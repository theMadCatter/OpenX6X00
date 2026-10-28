# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Chris Fink KD0CQ
"""The queue behind the radio's logbook uploads (Wavelog, QRZ, and the same shape for a
later eQSL, LoTW or Club Log).

The day's logs and the activation logs are ADIF files in <field>/logs. This module reads
them, tells one QSO from another, and keeps the ones not delivered yet in <field>/<slug>.queue
so a reboot or a network that is down does not lose them and does not send them twice.

A service is a small object: enabled(conf), deliver(conf, qso) and a few words for the line
the GUI already shows. deliver returns "ok", "dupe" or "reject <why>", or raises Retry
(try the whole run later), Fail (the operator has to change a setting) or Hold (this QSO
waits, the others still go). The line is the one x6100-wavelog already printed:

    off
    ok
    ok <n> <message>
    retry <n> <message>
    error <message>

<field> is /mnt/field, or X6100_FIELD_DIR. An ADIF length counts bytes, as the radio writes
it (src/field/flog.c).
"""

import calendar
import fcntl
import hashlib
import json
import os
import re
import time

KEEP_SENT = 7 * 86400   # a QSO sent is remembered a week; older ones the service itself finds again

FIELD_RE = re.compile(rb"<(\w+):(\d+)(?::\w)?>", re.I)


class Fail(Exception):
    """The run stops and the QSOs wait: a key or a setting, not a network blip."""


class Retry(Exception):
    """The run stops and the QSOs wait: no network, a busy server, an answer that is not the service."""


class Hold(Exception):
    """This QSO stays in the queue. The argument is the label in the line (a callsign)."""


def field_dir():
    return os.environ.get("X6100_FIELD_DIR", "/mnt/field")


def logs_dir():
    return os.path.join(field_dir(), "logs")


def user_agent():
    """What both uploads send. QRZ asks for an identifiable agent and limits a generic one."""
    return f"OpenX6X00/{os.environ.get('X6100_FW') or 'unknown'} (+https://github.com/Guru-RF/OpenX6X00)"


def one_line(text, n=80):
    return " ".join(str(text).split())[:n]


def load_json(path, default):
    try:
        with open(path, errors="replace") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path, data):
    """Replace the file whole: a power cut keeps the previous queue, not a half line."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f)
        f.flush()
        os.fsync(f.fileno())
    os.rename(tmp, path)


def adif_records(data):
    """The records of a log read as bytes: an ADIF length counts bytes (flog.c), "Jürgen" is 7"""
    m = re.search(rb"<eoh>", data, re.I)
    if m:
        data = data[m.end():]
    parts = re.split(rb"<eor>", data, flags=re.I)[:-1]   # what follows the last <eor> is not a record yet
    return [p.strip() for p in parts if re.search(rb"<call:\d+", p, re.I)]


def adif_fields(rec):
    """The fields of a record, bytes or text, counted in bytes"""
    b = rec.encode() if isinstance(rec, str) else rec
    out, i = {}, 0
    for m in FIELD_RE.finditer(b):
        if m.start() < i:
            continue
        n = int(m.group(2))
        out[m.group(1).decode().lower()] = b[m.end():m.end() + n].decode("utf-8", "replace")
        i = m.end() + n
    return out


def qso_time(f):
    d, t = f.get("qso_date", ""), (f.get("time_on", "") + "00")[:6]
    try:
        return calendar.timegm(time.strptime(d + t, "%Y%m%d%H%M%S"))
    except ValueError:
        return 0


def qso_key(f):
    """The same QSO in two reads of a log, or in two logs: the other station, the date and the time to the second,
    band, mode, and the own station callsign that also picks the profile with Automatic. Without the seconds and
    the station, a second QSO in the same minute, as ON6URE/P after one as ON6URE, was taken for the first and
    never sent (audit C11)"""
    mode = f.get("submode") or f.get("mode", "")
    me = f.get("station_callsign") or f.get("operator", "")
    return "|".join(x.strip().upper() for x in (f.get("call", ""), f.get("qso_date", ""),
                                                (f.get("time_on", "") + "00")[:6], f.get("band", ""), mode, me))


def qso_rev(f):
    """What the QSO holds, whatever the order and spacing of its fields: a report, name or reference corrected on
    the Logs page is a new revision of the same QSO, which was not sent again before (audit C11)"""
    return hashlib.sha1(json.dumps(sorted(f.items())).encode()).hexdigest()[:12]


def old_key(f):
    """The key the helper of v0.3.5 and v0.3.6 kept a QSO sent under, in "sent" as key -> QSO time. What it sent
    is not sent again here; and as it runs again on this state after Update > Roll back, each QSO sent here is in
    "sent" under its old key too (old_of), as a QSO time: that helper neither stops at the state (a TypeError at
    every run, "sent" never saved) nor sends again the QSOs this one sent. The revisions are in "revs", which it
    leaves as they are"""
    mode = f.get("submode") or f.get("mode", "")
    return "|".join(x.upper() for x in (f.get("call", ""), f.get("qso_date", ""), f.get("time_on", "")[:4],
                                        f.get("band", ""), mode))


def old_of(key):
    """The old_key of the QSO with this qso_key"""
    p = key.split("|")
    return "|".join(p[:2] + [p[2][:4]] + p[3:5])


def mark_sent(state, q):
    """q sent (or refused): not sent again as it stands, nor as any log holds it now (two copies of the QSO that
    differ, in two logs, went as one)"""
    state["sent"][q["key"]] = state["sent"][old_of(q["key"])] = q["t"]
    revs = state["revs"].setdefault(q["key"], [])
    revs.extend(r for r in dict.fromkeys([q["rev"], *q.get("logs", {}).values()]) if r not in revs)


def collect(state, queue, since):
    """The QSOs logged since `since` into the queue: a new one, or one changed since it was sent ("fix"). One that
    waits goes as it stands at the run, and not at all when no log holds it any more (deleted, or its call or time
    corrected meanwhile) or each log holds it as it was sent (an edit undone). The logs that did not change since
    the last run are not read again"""
    files = state.setdefault("files", {})
    sent = state.setdefault("sent", {})
    revs = state.setdefault("revs", {})
    ours = {old_of(k) for k in revs}   # the old keys written here: the others are of the helper before
    for q in queue:   # left waiting by the helper before the update
        if "rev" not in q:
            f = adif_fields(q["adif"])
            q["key"], q["rev"] = qso_key(f), qso_rev(f)
    waiting = {q["key"]: q for q in queue}
    now = time.time()
    try:
        names = sorted(n for n in os.listdir(logs_dir()) if n.endswith(".adi"))
    except OSError:
        return
    for name in names:
        path = os.path.join(logs_dir(), name)
        try:
            st = os.stat(path)
            stamp = [int(st.st_mtime), st.st_size]
            if files.get(name) == stamp or st.st_mtime < since:
                continue
            with open(path, "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        found, held = [], set()
        for rec in adif_records(data):
            f = adif_fields(rec)
            t, key, rev = qso_time(f), qso_key(f), qso_rev(f)
            held.add(key)
            if t < since:
                continue
            adif = rec.decode("utf-8", "replace") + "<eor>"
            q = waiting.get(key)
            if q:   # as this log holds it now; what goes is a copy not sent yet
                q.setdefault("logs", {})[name] = rev
                if rev not in revs.get(key, ()):
                    q.update(adif=adif, rev=rev)
                continue
            old = old_key(f)
            if key not in revs and (key in sent or old in sent and old not in ours):
                revs[key], sent[key], sent[old_of(key)] = [rev], t, t   # sent by the helper before the update
                ours.add(old_of(key))                                  # (or after a roll back), as it stands
            done = revs.get(key)
            if done and rev in done:
                continue
            q = {"key": key, "rev": rev, "call": f.get("call", "").upper(), "t": t, "adif": adif, "logs": {name: rev}}
            if done:
                q["fix"] = True
            found.append((t, q))
            waiting[key] = q
        for q in queue:   # waiting, and this log holds it no more: deleted, or its call or time corrected
            if name in q.get("logs", ()) and q["key"] not in held:
                del q["logs"][name]
        queue.extend(q for _, q in sorted(found, key=lambda x: x[0]))
        # FAT keeps a file's time in 2 s steps: a log changed again within them to the same size (a report
        # corrected, 59 to 57) has the same stamp, so one this recent is read again at the next run
        if now - st.st_mtime >= 2:
            files[name] = stamp
        else:
            files.pop(name, None)
    for name in list(files):   # deleted logs
        if name not in names:
            del files[name]
    # A QSO waits while a log holds it in a form not sent yet: deleted, its call or time corrected (no log holds it
    # now), or edited back to what was sent, it does not go. One the helper before queued, in no log read since, goes
    queue[:] = [q for q in queue if "logs" not in q
                or any(r not in revs.get(q["key"], ()) for r in q["logs"].values())]


def qsos(n):
    return f"{n} QSO{'' if n == 1 else 's'}"


def run_sync(svc, conf):
    """One pass of svc: new QSOs into its queue, then the queue to the service. The line is svc's to print."""
    if not svc.enabled(conf):
        return "off"
    d = field_dir()
    os.makedirs(d, exist_ok=True)
    lock_path = os.path.join(d, f".{svc.slug}.lock")
    state_path = os.path.join(d, f"{svc.slug}.state")
    queue_path = os.path.join(d, f"{svc.slug}.queue")
    failed_path = os.path.join(d, f"{svc.slug}.failed.adi")
    with open(lock_path, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)   # the GUI and the web page's Send now: one run of this service at a time
        state = load_json(state_path, {})
        queue = load_json(queue_path, [])
        since = int(conf.get("since") or 0) or int(time.time())
        collect(state, queue, since)
        save_json(queue_path, queue)
        save_json(state_path, state)
        ok, dupe, bad, last, held, kept = 0, 0, [], "", [], []
        name = svc.name
        try:
            i = 0
            while i < len(queue):
                q = queue[i]
                try:
                    res = svc.deliver(conf, q)
                except Hold as e:   # it waits; the others still go
                    held.append(str(e) or q["call"])
                    i += 1
                    continue
                if res.startswith("reject"):
                    bad.append(f"{q['call']}: {res[7:]}")
                    with open(failed_path, "a") as fh:
                        fh.write(q["adif"] + "\n")
                else:
                    ok, dupe, last = ok + (res == "ok"), dupe + (res == "dupe"), q["call"]
                    if res == "dupe" and q.get("fix"):   # the service adds QSOs, it changes none it has
                        kept.append(q["call"])
                mark_sent(state, q)
                queue.pop(i)
                save_json(queue_path, queue)
            n = ok + dupe
            said = ", ".join(([f"{qsos(ok)} uploaded"] if ok else []) + ([f"{qsos(dupe)} already there"] if dupe else []))
            result = ("ok " + (f"{n} {name}: {last} uploaded" if n == 1 and ok
                               else f"{n} {name}: {last} was already there" if n == 1
                               else f"{n} {name}: {said}") if n else "ok")
            if kept:
                result += ", edit it in " + name + " too" if n == 1 else \
                    f", edit {kept[0]} in {name} too" + (f" (+{len(kept) - 1} more)" if len(kept) > 1 else "")
            if bad:
                result = f"error {name} refused {bad[0]}" + (f" (+{len(bad) - 1} more)" if len(bad) > 1 else "")
            elif held:
                done = f"{last} uploaded, " if n == 1 and ok else f"{said}, " if n else ""
                result = f"error {name}: {done}{svc.hold_phrase(held[0])}, {qsos(len(queue))} waiting"
        except Retry as e:   # the display says why in short; the web page has the last result
            why = str(e) if str(e).startswith(tuple(svc.retry_literal)) else "no connection"
            result = f"retry {len(queue)} {name}: {why}, {qsos(len(queue))} waiting"
        except Fail as e:
            result = f"error {name}: {e}, {qsos(len(queue))} waiting"
        now = time.time()
        state["sent"] = {k: t for k, t in state["sent"].items() if t > now - KEEP_SENT}
        state["revs"] = {k: r for k, r in state["revs"].items() if k in state["sent"]}
        state["last"] = {"time": int(now), "result": result, "queue": len(queue)}
        save_json(queue_path, queue)
        save_json(state_path, state)
        return result
