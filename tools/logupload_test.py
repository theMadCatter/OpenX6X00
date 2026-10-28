#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Chris Fink KD0CQ
"""Host checks for the shared log upload queue and the QRZ helper.

Nothing here contacts logbook.qrz.com or a real Wavelog. urllib.request.urlopen is
replaced, and both helpers are pointed at 127.0.0.1:9. Run from the repo root:

    python3 tools/logupload_test.py
"""

import io
import os
import runpy
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "webserver" / "src"))

# Read once, before the helpers are loaded: x6100-qrzlog takes the URL at import.
os.environ["X6100_QRZ_API"] = "http://127.0.0.1:9/api"
os.environ.pop("X6100_QRZLOG_KEY", None)
os.environ.pop("X6100_WAVELOG_URL", None)
os.environ.pop("X6100_WAVELOG_KEY", None)

QRZ = runpy.run_path(str(ROOT / "rootfs" / "usr" / "sbin" / "x6100-qrzlog"))
WAVE = runpy.run_path(str(ROOT / "rootfs" / "usr" / "sbin" / "x6100-wavelog"))


def adif(**fields):
    """One record. Lengths are bytes, as the radio writes them."""
    parts = []
    for name, value in fields.items():
        raw = str(value).encode()
        parts.append(f"<{name}:{len(raw)}>{value}")
    return "".join(parts) + "<eor>\n"


def qso(**over):
    fields = {
        "CALL": "DL1ABC", "QSO_DATE": "20261027", "TIME_ON": "120001", "BAND": "20m",
        "MODE": "SSB", "STATION_CALLSIGN": "KD0CQ", "RST_SENT": "59", "NAME": "Peter",
    }
    fields.update(over)
    return adif(**fields)


class Answer:
    """One scripted urlopen: a body with this HTTP code, an HTML page, or a network error."""

    def __init__(self, code=200, body="", network=None):
        self.code = code
        self.body = body
        self.network = network


class Gate:
    """urlopen stand-in. Refuses any host that is not 127.0.0.1."""

    def __init__(self, answers):
        self.answers = list(answers)
        self.posts = []

    def __call__(self, req, timeout=None, context=None):
        host = urllib.parse.urlsplit(req.full_url).hostname
        if host != "127.0.0.1":
            raise AssertionError(f"live host contacted: {req.full_url}")
        self.posts.append(req)
        if not self.answers:
            raise AssertionError(f"unexpected request {req.full_url}")
        ans = self.answers.pop(0)
        if ans.network:
            raise urllib.error.URLError(ans.network)
        raw = ans.body.encode()
        if ans.code >= 400:
            raise urllib.error.HTTPError(req.full_url, ans.code, "err", None, io.BytesIO(raw))
        return _Resp(ans.code, raw)


class _Resp:
    def __init__(self, status, body):
        self.status = status
        self._body = body

    def read(self):
        return self._body

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="x6qrz-")
        self.field = Path(self.tmp.name)
        (self.field / "logs").mkdir()
        os.environ["X6100_FIELD_DIR"] = str(self.field)
        os.environ.pop("X6100_QRZLOG_KEY", None)
        self._urlopen = urllib.request.urlopen

    def tearDown(self):
        urllib.request.urlopen = self._urlopen
        os.environ.pop("X6100_QRZLOG_KEY", None)
        self.tmp.cleanup()

    def script(self, answers):
        gate = Gate(answers)
        urllib.request.urlopen = gate
        return gate

    def conf(self, text):
        path = self.field / "qrz.conf"
        path.write_text(text)
        os.chmod(path, 0o600)

    def log(self, text, name="day.adi"):
        (self.field / "logs" / name).write_text(text)

    def sync(self):
        return QRZ["cmd_sync"](QRZ["read_conf"]())

    def queue(self):
        import json
        path = self.field / "qrzlog.queue"
        return json.loads(path.read_text()) if path.exists() else []

    def failed(self):
        path = self.field / "qrzlog.failed.adi"
        return path.read_text() if path.exists() else ""

    def test_off_without_switch_or_key(self):
        gate = self.script([])
        self.conf("active=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        self.assertEqual(self.sync(), "off")
        self.conf("autoupload=1\n")
        self.assertEqual(self.sync(), "off")
        self.assertEqual(gate.posts, [])

    def test_upload_ok_then_not_again(self):
        gate = self.script([Answer(body="RESULT=OK&COUNT=1")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso(NAME="Trinidad & Tobago"))
        line = self.sync()
        self.assertEqual(line, "ok 1 QRZ: DL1ABC uploaded")
        self.assertEqual(self.queue(), [])
        self.assertEqual(self.failed(), "")
        body = gate.posts[0].data
        form = urllib.parse.parse_qs(body.decode())
        self.assertEqual(form["KEY"], ["SECRET"])
        self.assertEqual(form["ACTION"], ["INSERT"])
        self.assertIn("Trinidad & Tobago", form["ADIF"][0])
        self.assertNotIn(b"Trinidad & Tobago", body)   # the '&' must not split the form
        self.assertNotIn(b"SECRET", line.encode())
        again = self.sync()
        self.assertEqual(again, "ok")
        self.assertEqual(len(gate.posts), 1)

    def test_duplicate_is_counted_not_retried(self):
        gate = self.script([Answer(body="RESULT=FAIL&REASON=Duplicate QSO")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        line = self.sync()
        self.assertIn("was already there", line)
        self.assertNotIn("refused", line)
        self.assertEqual(self.queue(), [])
        self.assertEqual(self.failed(), "")
        self.sync()
        self.assertEqual(len(gate.posts), 1)

    def test_network_then_ok_after_reload(self):
        gate = self.script([Answer(network="timed out"), Answer(body="RESULT=OK")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        line = self.sync()
        self.assertTrue(line.startswith("retry "), line)
        self.assertIn("no connection", line)
        self.assertEqual(len(self.queue()), 1)
        # a new process, as after a reboot: the queue is the file
        line = self.sync()
        self.assertEqual(line, "ok 1 QRZ: DL1ABC uploaded")
        self.assertEqual(self.queue(), [])
        self.assertEqual(len(gate.posts), 2)

    def test_http_500_and_captive_portal_retry(self):
        gate = self.script([
            Answer(code=500, body="RESULT=FAIL&REASON=down"),
            Answer(code=200, body="<html><title>login</title></html>"),
        ])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        line = self.sync()
        self.assertIn("server HTTP 500", line)
        self.assertEqual(len(self.queue()), 1)
        line = self.sync()
        self.assertIn("not QRZ", line)
        self.assertTrue(line.startswith("retry "), line)
        self.assertEqual(len(self.queue()), 1)
        self.assertEqual(len(gate.posts), 2)

    def test_auth_and_401_keep_the_queue(self):
        for ans in (Answer(body="RESULT=AUTH&REASON=bad key"), Answer(code=401, body="RESULT=AUTH")):
            with self.subTest(code=ans.code):
                self.script([ans])
                self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
                self.log(qso())
                if (self.field / "qrzlog.state").exists():
                    (self.field / "qrzlog.state").unlink()
                if (self.field / "qrzlog.queue").exists():
                    (self.field / "qrzlog.queue").unlink()
                line = self.sync()
                self.assertTrue(line.startswith("error "), line)
                self.assertEqual(len(self.queue()), 1)
                self.assertEqual(self.failed(), "")

    def test_reject_is_filed_and_not_resent(self):
        gate = self.script([Answer(body="RESULT=FAIL&REASON=invalid call")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        line = self.sync()
        self.assertIn("refused", line)
        self.assertIn("DL1ABC", self.failed())
        self.assertEqual(self.queue(), [])
        self.assertEqual(self.sync(), "ok")
        self.assertEqual(len(gate.posts), 1)

    def test_edit_of_a_sent_qso_says_edit_it_there_too(self):
        self.script([Answer(body="RESULT=OK"), Answer(body="RESULT=FAIL&REASON=duplicate")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso(RST_SENT="59"))
        self.assertEqual(self.sync(), "ok 1 QRZ: DL1ABC uploaded")
        path = self.field / "logs" / "day.adi"
        path.write_text(qso(RST_SENT="57"))   # same length: the identity is unchanged, the record is not
        later = time.time() + 10              # a same-second stamp must not hide the edit
        os.utime(path, (later, later))
        line = self.sync()
        self.assertIn("edit it in QRZ too", line)
        self.assertEqual(self.queue(), [])

    def test_deleted_while_queued_is_not_sent(self):
        gate = self.script([Answer(network="down"), Answer(body="RESULT=OK")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        self.assertTrue(self.sync().startswith("retry "))
        self.log("")   # the record is gone; the file is still there so the queue can see that
        line = self.sync()
        self.assertEqual(line, "ok")
        self.assertEqual(self.queue(), [])
        self.assertEqual(len(gate.posts), 1)

    def test_profiles_match_the_station_callsign(self):
        gate = self.script([Answer(body="RESULT=OK"), Answer(body="RESULT=OK")])
        self.conf("active=1\nautoupload=1\nsince=1\n"
                  "call1=KD0CQ\nkey1=KEY-HOME\ncall2=KD0CQ/P\nkey2=KEY-PORTABLE\n")
        self.log(qso(TIME_ON="120001", STATION_CALLSIGN="KD0CQ")
                 + qso(TIME_ON="120002", STATION_CALLSIGN="KD0CQ/P", CALL="G4ABC"))
        line = self.sync()
        self.assertIn("uploaded", line)
        keys = [urllib.parse.parse_qs(p.data.decode())["KEY"][0] for p in gate.posts]
        self.assertEqual(keys, ["KEY-HOME", "KEY-PORTABLE"])
        self.assertEqual(self.queue(), [])

    def test_unmatched_call_waits_and_the_other_goes(self):
        gate = self.script([Answer(body="RESULT=OK")])
        self.conf("active=1\nautoupload=1\nsince=1\ncall1=KD0CQ\nkey1=KEY-HOME\n")
        self.log(qso(TIME_ON="120001", STATION_CALLSIGN="W1AW", CALL="G4ABC")
                 + qso(TIME_ON="120002", STATION_CALLSIGN="KD0CQ"))
        line = self.sync()
        self.assertIn("no logbook for W1AW", line)
        self.assertEqual(len(gate.posts), 1)
        self.assertEqual(urllib.parse.parse_qs(gate.posts[0].data.decode())["KEY"], ["KEY-HOME"])
        self.assertEqual([q["call"] for q in self.queue()], ["G4ABC"])

    def test_legacy_key_takes_any_qso(self):
        gate = self.script([Answer(body="RESULT=OK")])
        self.conf("autoupload=1\nsince=1\nkey=LEGACY\n")
        self.log(qso(STATION_CALLSIGN="W1AW"))
        self.assertIn("uploaded", self.sync())
        self.assertEqual(urllib.parse.parse_qs(gate.posts[0].data.decode())["KEY"], ["LEGACY"])

    def test_since_in_the_future_skips_the_log(self):
        gate = self.script([])
        self.conf("active=1\nautoupload=1\nsince=2000000000\ncall1=KD0CQ\nkey1=SECRET\n")
        self.log(qso())
        self.assertEqual(self.sync(), "ok")
        self.assertEqual(gate.posts, [])
        self.assertEqual(self.queue(), [])

    def test_wavelog_created_and_duplicate_lines(self):
        """The lines the GUI already shows. A refactor of the queue must not change them."""
        gate = self.script([
            Answer(code=201, body='{"status":"created"}'),
            Answer(code=400, body='{"status":"abort","messages":["Duplicate of an existing QSO"]}'),
        ])
        (self.field / "wavelog.conf").write_text(
            "enabled=1\nurl=http://127.0.0.1:9\nkey=WAVEKEY\nstation_id=42\nsince=1\nverify_tls=1\n")
        self.log(qso())
        line = WAVE["cmd_sync"](WAVE["read_conf"]())
        self.assertEqual(line, "ok 1 Wavelog: DL1ABC uploaded")
        self.log(qso(CALL="G4ABC", TIME_ON="130000"))
        line = WAVE["cmd_sync"](WAVE["read_conf"]())
        self.assertIn("was already there", line)
        self.assertNotIn("logbook.qrz.com", gate.posts[0].full_url)
        self.assertIn("127.0.0.1", gate.posts[0].full_url)

    def test_wavelog_script_still_exports_what_the_audit_reads(self):
        self.assertIn("adif_fields", WAVE)
        self.assertIn("qso_key", WAVE)
        fields = WAVE["adif_fields"]("<CALL:6>DL1ABC<MODE:3>SSB<eor>")
        self.assertEqual(fields["call"], "DL1ABC")
        self.assertTrue(WAVE["qso_key"](fields))

    def test_typed_key_is_used_for_test_and_not_printed(self):
        gate = self.script([Answer(body="RESULT=OK&CALL=KD0CQ&COUNT=10&DATA=OK")])
        os.environ["X6100_QRZLOG_KEY"] = "TYPED-SECRET"
        line = QRZ["cmd_test"]({})
        self.assertTrue(line.startswith("ok "), line)
        self.assertNotIn("TYPED-SECRET", line)
        self.assertIn("KD0CQ", line)
        self.assertIn(b"TYPED-SECRET", gate.posts[0].data)
        self.assertIn(b"ACTION=STATUS", gate.posts[0].data)

    def test_field_page_write_keeps_autoupload(self):
        """Operating > QRZ and Field Ops share qrz.conf. A Field Ops save must not drop the switch."""
        from x6100_webserver import apps, settings
        settings.FIELD_DIR = str(self.field)
        apps.qrz_write({"active": 1, "calls": ["KD0CQ", "", ""], "keys": ["SECRET", "", ""],
                        "autoupload": True, "since": 12345})
        mode = (self.field / "qrz.conf").stat().st_mode & 0o777
        self.assertEqual(mode, 0o600)
        c = apps.qrz_read()
        c["calls"][0] = "KD0CQ/P"
        apps.qrz_write(c)
        text = (self.field / "qrz.conf").read_text()
        self.assertIn("autoupload=1", text)
        self.assertIn("since=12345", text)
        self.assertIn("call1=KD0CQ/P", text)
        self.assertIn("key1=SECRET", text)
        c["keys"] = ["", "", ""]
        apps.qrz_write(c)
        self.assertFalse((self.field / "qrz.conf").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
