"""Routes of the Bands, Transverters, Digital modes, Time and Files pages, of the plain pages and of the static files.
They join the app of apps.py, which imports this module at its end."""

from datetime import datetime, timezone
try:
    import gzip
except ImportError:   # images built before python3-zlib was selected
    gzip = None
import functools
import json
import os
import re
import sqlite3
import subprocess
import urllib.parse

import bottle

from . import freq_lists
from . import settings
from .apps import PACKAGE_DIR, api_error, app, display_call

STATIC_PATH = os.path.join(PACKAGE_DIR, "static")


# Bands and digital mode frequencies (Bands and Digital modes pages; freq_lists.py does the checks)

def json_list(items):
    """A JSON array: bottle makes JSON of dicts only."""
    bottle.response.content_type = 'application/json'
    return json.dumps(items)


def freq_list_change(change, *args, created=False):
    """Runs change(dbcon, ..., request JSON): {"status": "OK"} (201 for a new entry) or 400 with the reason."""
    try:
        data = bottle.request.json
    except ValueError:   # not JSON at all
        data = None
    try:
        found = change(*args, data)
    except freq_lists.Invalid as e:
        return api_error(400, str(e))
    if found is False:
        return api_error(404, "No such frequency")
    if created:
        bottle.response.status = 201
    return {"status": "OK"}


def band_tx(lo, hi):
    """Whether the radio transmits somewhere in [lo, hi] Hz, as the GUI's band_use has it (src/cfg_socket.cpp: HF, the
    transverters that are on); HF alone when the GUI does not answer. For the TX shift on the Bands page."""
    reply = display_call({"cmd": "band_use", "lo": lo, "hi": hi})
    if reply and reply.get("t") == "band_use":
        return reply.get("use") == 2
    return freq_lists.hf_tx(lo, hi)


@app.get('/api/bands')
def bands_get(dbcon):
    return json_list(freq_lists.bands(dbcon))


@app.put('/api/bands')
def bands_add(dbcon):
    return freq_list_change(functools.partial(freq_lists.add_band, band_tx=band_tx), dbcon, created=True)


@app.post('/api/bands/<band_id:int>')
def bands_set(band_id, dbcon):
    return freq_list_change(functools.partial(freq_lists.update_band, band_tx=band_tx), dbcon, band_id)


@app.delete('/api/bands/<band_id:int>')
def bands_remove(band_id, dbcon):
    freq_lists.delete_band(dbcon, band_id)
    return {"status": "OK"}


# Transverters. Twelve slots in the table the two-slot page already used
# (id, name, val). Numbers and the slice test are the same as src/xvtr.h and
# src/cfg/xvtr_cfg.cpp: the dial is the true RF, the radio is sent an IF inside
# 100 kHz..55 MHz. This process does not know X6100 from X6200. An empty table
# is the presets, all off; the radio writes the resolved rows (2 m and 70 cm
# on for an X6100) the first time it starts. A shift the old page stored, and
# no "en" row, still means on when the shift is not zero.

XVTR_COUNT = 12
XVTR_RF_MIN = 50_000_000
XVTR_RF_MAX = 30_000_000_000
XVTR_IF_MIN = 100_000
XVTR_IF_MAX = 55_000_000
XVTR_CAL_MAX = 10_000_000
XVTR_PWR_MAX = 100
XVTR_LO_MAX = XVTR_RF_MAX + XVTR_IF_MAX
XVTR_LEGACY_SHIFT_MAX = 500_000_000          # the old page's cap, Hz
XVTR_NAME_MAX = 15

# src/xvtr.c presets[]. The LO is the weak-signal calling frequency minus a 28 MHz IF, low-side.
XVTR_PRESETS = [
    ("2m", 144_000_000, 150_000_000, 116_000_000),
    ("70cm", 432_000_000, 438_000_000, 404_000_000),
    ("1.25m", 222_000_000, 225_000_000, 194_000_000),
    ("33cm", 902_000_000, 928_000_000, 874_000_000),
    ("23cm", 1_240_000_000, 1_300_000_000, 1_212_000_000),
    ("13cm", 2_300_000_000, 2_450_000_000, 2_272_000_000),
    ("9cm", 3_300_000_000, 3_500_000_000, 3_372_000_000),
    ("6cm", 5_650_000_000, 5_925_000_000, 5_732_000_000),
    ("3cm", 10_000_000_000, 10_500_000_000, 10_340_000_000),
    ("1.25cm", 24_000_000_000, 24_250_000_000, 24_020_000_000),
]


def xvtr_preset(index):
    """Factory row, off. Slots past the list are empty spares."""
    if index < 0 or index >= len(XVTR_PRESETS):
        return {"name": "", "rf_from": 0, "rf_to": 0, "lo": 0, "cal": 0,
                "high": 0, "enabled": 0, "rx_only": 0, "pwr": 0}
    name, rf_from, rf_to, lo = XVTR_PRESETS[index]
    return {"name": name, "rf_from": rf_from, "rf_to": rf_to, "lo": lo, "cal": 0,
            "high": 0, "enabled": 0, "rx_only": 0, "pwr": 0}


def xvtr_clean_name(text, index):
    """Printable ASCII, 15 characters. An empty name becomes the preset's."""
    if not isinstance(text, str):
        raise ValueError(f"Slot {index + 1}: name")
    out = []
    for ch in text:
        o = ord(ch)
        if o == 0:
            break
        out.append(ch if 32 <= o < 127 else " ")
        if len(out) >= XVTR_NAME_MAX:
            break
    name = "".join(out)
    if not name:
        name = xvtr_preset(index)["name"] or "XVTR"
    return name


def xvtr_slice_ok(slot):
    """True when some RF in [from, to] maps into the HF IF window. Same test as xvtr_slice."""
    lo = slot["lo"] + slot["cal"]
    if slot["high"]:
        a, b = lo - XVTR_IF_MAX, lo - XVTR_IF_MIN
    else:
        a, b = lo + XVTR_IF_MIN, lo + XVTR_IF_MAX
    if a < slot["rf_from"]:
        a = slot["rf_from"]
    if b > slot["rf_to"]:
        b = slot["rf_to"]
    return a <= b


def _xvtr_int(value, what):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(what)
    if isinstance(value, float):
        if value != int(value):
            raise ValueError(what)
        value = int(value)
    return value


def _xvtr_flag(value, what):
    if value in (0, 1, True, False):
        return 1 if value else 0
    raise ValueError(what)


def _db_int(value):
    """A cell of the transverter table: SQLite hands an int back, or text the old page wrote."""
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    return int(str(value).strip() or "0")


def xvtr_read(dbcon):
    """The twelve slots, with the same missing-key rules as cfg_xvtr_load."""
    stored = {}
    for id_, name, val in dbcon.execute("SELECT id, name, val FROM transverter"):
        stored.setdefault(id_, {})[name] = val
    slots = []
    for i in range(XVTR_COUNT):
        s = xvtr_preset(i)
        v = stored.get(i, {})

        def has(key, row=v):
            return key in row

        if has("from"):
            s["rf_from"] = _db_int(v["from"])
        if has("to"):
            s["rf_to"] = _db_int(v["to"])
        if has("lo"):
            s["lo"] = _db_int(v["lo"])
        elif has("shift") and _db_int(v["shift"]) != 0:
            s["lo"] = _db_int(v["shift"])
        if has("en"):
            s["enabled"] = 1 if _db_int(v["en"]) else 0
        elif has("shift"):
            s["enabled"] = 1 if _db_int(v["shift"]) != 0 else 0
        if has("side"):
            s["high"] = 1 if _db_int(v["side"]) else 0
        if has("rx"):
            s["rx_only"] = 1 if _db_int(v["rx"]) else 0
        if has("pwr"):
            pwr = _db_int(v["pwr"])
            s["pwr"] = 0 if pwr < 0 else min(pwr, XVTR_PWR_MAX)
        if has("cal"):
            s["cal"] = _db_int(v["cal"])
        if has("name") and v["name"] not in (None, ""):
            s["name"] = xvtr_clean_name(str(v["name"]), i)
        slots.append(s)
    return slots, not stored


def xvtr_check(index, raw):
    """One posted slot, or ValueError with the reason. An off empty slot (from and to both 0) is kept."""
    if not isinstance(raw, dict):
        raise ValueError(f"Slot {index + 1} is not an object")
    label = f"Slot {index + 1}"
    slot = {
        "name": xvtr_clean_name(raw.get("name", ""), index),
        "rf_from": _xvtr_int(raw.get("rf_from"), f"{label}: from"),
        "rf_to": _xvtr_int(raw.get("rf_to"), f"{label}: to"),
        "lo": _xvtr_int(raw.get("lo"), f"{label}: LO"),
        "cal": _xvtr_int(raw.get("cal", 0), f"{label}: calibration"),
        "high": _xvtr_flag(raw.get("high", 0), f"{label}: high LO"),
        "enabled": _xvtr_flag(raw.get("enabled", 0), f"{label}: on"),
        "rx_only": _xvtr_flag(raw.get("rx_only", 0), f"{label}: RX only"),
        "pwr": _xvtr_int(raw.get("pwr", 0), f"{label}: drive"),
    }
    if slot["pwr"] < 0 or slot["pwr"] > XVTR_PWR_MAX:
        raise ValueError(f"{label}: drive is 0 to 10.0 W")
    if slot["cal"] < -XVTR_CAL_MAX or slot["cal"] > XVTR_CAL_MAX:
        raise ValueError(f"{label}: calibration is -10000 to 10000 kHz")
    if not slot["enabled"] and slot["rf_from"] == 0 and slot["rf_to"] == 0:
        return slot
    if slot["rf_from"] < XVTR_RF_MIN or slot["rf_to"] > XVTR_RF_MAX or slot["rf_from"] >= slot["rf_to"]:
        raise ValueError(f"{label}: RF from 50 MHz to 30 GHz, and from below to")
    if slot["lo"] < 0 or slot["lo"] > XVTR_LO_MAX:
        raise ValueError(f"{label}: LO out of range")
    if slot["enabled"] and not xvtr_slice_ok(slot):
        raise ValueError(f"{label}: that LO does not reach the radio")
    return slot


def xvtr_legacy_shift(slot):
    """The shift the old two-slot page can still tune. High-side and a large LO write 0."""
    if slot["enabled"] and not slot["high"] and 0 < slot["lo"] <= XVTR_LEGACY_SHIFT_MAX:
        return slot["lo"]
    return 0


@app.get("/api/transverters")
def transverters_get(dbcon):
    slots, empty = xvtr_read(dbcon)
    out = {"slots": slots}
    if empty:
        out["note"] = ("No transverters saved yet. The radio writes its defaults the first time it starts: "
                       "2 m and 70 cm on for an X6100, off for an X6200.")
    return out


@app.post("/api/transverters")
def transverters_set(dbcon):
    try:
        data = bottle.request.json
    except ValueError:
        data = None
    raw = data.get("slots") if isinstance(data, dict) else data
    if not isinstance(raw, list) or len(raw) != XVTR_COUNT:
        return api_error(400, "Send all 12 transverters")
    try:
        slots = [xvtr_check(i, raw[i]) for i in range(XVTR_COUNT)]
    except ValueError as ex:
        return api_error(400, str(ex))
    try:
        with dbcon:
            for i, slot in enumerate(slots):
                dbcon.executemany(
                    "INSERT OR REPLACE INTO transverter(id, name, val) VALUES(?, ?, ?)",
                    [(i, "from", slot["rf_from"]), (i, "to", slot["rf_to"]), (i, "lo", slot["lo"]),
                     (i, "shift", xvtr_legacy_shift(slot)), (i, "side", slot["high"]),
                     (i, "en", slot["enabled"]), (i, "rx", slot["rx_only"]),
                     (i, "pwr", slot["pwr"]), (i, "cal", slot["cal"]), (i, "name", slot["name"])])
    except sqlite3.Error as ex:
        return api_error(500, f"Can't save: {ex}")
    # The GUI keeps the slots in memory. A radio that is up reloads them; one that is not
    # reads the table at the next start. Either way the save itself stands.
    reply = display_call({"cmd": "xvtr_reload"})
    return {"status": "OK", "live": bool(reply and reply.get("t") == "ok"), "slots": slots}


@app.get('/api/digital_modes')
def freqs_get(dbcon):
    return json_list(freq_lists.digital_modes(dbcon))


@app.put('/api/digital_modes')
def freqs_add(dbcon):
    return freq_list_change(freq_lists.add_digital_mode, dbcon, created=True)


@app.post('/api/digital_modes/<mode_id:int>')
def freqs_set(mode_id, dbcon):
    return freq_list_change(freq_lists.update_digital_mode, dbcon, mode_id)


@app.delete('/api/digital_modes/<mode_id:int>')
def freqs_remove(mode_id, dbcon):
    freq_lists.delete_digital_mode(dbcon, mode_id)
    return {"status": "OK"}


# Static files, plain pages and the file browser

@app.get('/static/<filepath:path>')
def static_files(filepath):
    # The radio's WiFi is slow: send static files gzipped and let the browser keep them
    response = bottle.static_file(filepath, root=STATIC_PATH)
    if isinstance(response, bottle.HTTPError) or response.status_code != 200:
        return response
    response.set_header('Cache-Control', 'max-age=86400')
    if gzip and 'gzip' in bottle.request.headers.get('Accept-Encoding', ''):
        if filepath not in _gzip_cache:
            with open(os.path.join(STATIC_PATH, filepath), 'rb') as f:
                _gzip_cache[filepath] = gzip.compress(f.read())
        response.body.close()
        response.body = _gzip_cache[filepath]
        response.set_header('Content-Encoding', 'gzip')
        response.set_header('Content-Length', str(len(response.body)))
    return response


_gzip_cache = {}


@app.get('/')
@app.get('/<view:re:bands|digital_modes|time|transverters>')
def plain_page(view='index'):
    """Pages that are only their template: the data comes from the API."""
    return bottle.template(view)


@app.get('/files/')
@app.get('/files/<filepath:path>')
@app.get('/files/<filepath:path>/')   # kept for old links: the route above takes the slash too
def file_browser(filepath=''):
    """The DATA partition (FILEBROWSER_PATH): a folder lists its entries, a file is downloaded.
    Nothing outside it: no '..' above it, no absolute path."""
    root = os.path.abspath(settings.FILEBROWSER_PATH)
    target = os.path.abspath(os.path.join(root, filepath.lstrip('/')))
    if os.path.commonpath([root, target]) != root or filepath.startswith('/'):
        raise bottle.HTTPError(403, 'Access denied.')
    if os.path.isfile(target):
        os.sync()   # what the GUI has just written, complete
        response = bottle.static_file(os.path.relpath(target, root), root=root, download=True)
        response.set_header('Cache-Control', 'private, no-cache, no-store')
        return response
    try:
        names = sorted(os.listdir(target))
    except OSError:
        raise bottle.HTTPError(404, 'Not found')
    folders = {n for n in names if os.path.isdir(os.path.join(target, n))}
    return bottle.template('files', dirs=[n for n in names if n in folders],
                           files=[n for n in names if n not in folders], href=urllib.parse.quote)


# Time page: the radio clock (set by hand or by NTP) and its timezone (/etc/localtime)

ZONE_NAME = re.compile(r'[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)*')    # Europe/Rome, Etc/GMT+1: no '..', no '/' first
NTP_SERVER = re.compile(r'[A-Za-z0-9][A-Za-z0-9.:-]{0,252}')     # name or address, never an ntpdate option


def time_request():
    try:
        data = bottle.request.json
    except ValueError:
        data = None
    return data if isinstance(data, dict) else {}


@app.get('/api/get_time')
def clock_get():
    return {"server_time": datetime.now(timezone.utc).isoformat()}


def clock_from_ntp(server):
    if not isinstance(server, str) or not NTP_SERVER.fullmatch(server):
        return api_error(400, "Invalid NTP server address")
    try:
        r = subprocess.run(["ntpdate", "-u", server], capture_output=True, timeout=20)
    except subprocess.TimeoutExpired:
        return api_error(500, "NTP update timeout")
    except OSError as e:
        return api_error(500, f"NTP update failed: {e}")
    if r.returncode != 0:
        return api_error(500, "NTP update failed: " + r.stderr.decode(errors="replace"))
    return {"status": "success", "msg": "NTP update successful"}


@app.post('/api/update_time')
def clock_set():
    """update_mode "ntp" with server_address, or "manual" with manual_time "YYYY-MM-DD HH:MM:SS" (radio local time)."""
    data = time_request()
    how = data.get("update_mode")
    if not how:
        return api_error(400, "update_mode is required")
    if how == "ntp":
        return clock_from_ntp(data.get("server_address"))
    if how != "manual":
        return api_error(400, f"unknown update_mode: {how}")
    when = data.get("manual_time")
    if not when:
        return api_error(400, "manual_time is required")
    try:
        when = datetime.strptime(when, "%Y-%m-%d %H:%M:%S")
        subprocess.run(["date", "-s", when.strftime("%Y-%m-%d %H:%M:%S")], check=True)
    except (TypeError, ValueError, OSError, subprocess.SubprocessError) as e:
        return api_error(500, f"Failed to set manual time: {e}")
    return {"status": "success", "msg": "Server time updated manually"}


@app.get('/api/get_timezone')
def zone_get():
    """The zone name of the /etc/localtime link ("Etc/UTC", "Europe/Rome")."""
    path = os.path.realpath(settings.LOCALTIME)
    if not os.path.exists(path):
        return api_error(500, f"Failed to fetch timezone: {settings.LOCALTIME} is missing")
    marker = "/posix/" if "/posix/" in path else "/zoneinfo/"
    return {"timezone": path.rpartition(marker)[2]}


@app.post('/api/set_timezone')
def zone_set():
    zone = time_request().get("timezone")
    if not zone:
        return api_error(400, "Timezone is required")
    target = os.path.join(settings.ZONEINFO, str(zone))
    if not (isinstance(zone, str) and ZONE_NAME.fullmatch(zone) and os.path.isfile(target)):
        return api_error(400, f"Invalid timezone: {zone}")
    tmp = settings.LOCALTIME + ".new"
    try:   # a new link renamed over the old one: never a moment without /etc/localtime
        if os.path.lexists(tmp):
            os.remove(tmp)
        os.symlink(target, tmp)
        os.replace(tmp, settings.LOCALTIME)
    except OSError as e:
        return api_error(500, f"Failed to set timezone: {e}")
    return {"status": "success", "msg": "Timezone updated successfully"}
