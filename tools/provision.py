#!/usr/bin/env python3
"""Write the panel's settings to its flash over USB, from your config.ini.

    python3 tools/provision.py                  # config.ini -> panel
    python3 tools/provision.py --check          # validate config.ini and test the data key; change nothing
    python3 tools/provision.py --firmware FILE  # first flash a firmware image, then the settings

Everything configurable lives in config.ini (copy config.example.ini and edit it). The settings go into the
panel's NVS partition, so the firmware is generic: change a setting by editing config.ini and running this
again. No rebuild, no code changes. Reflashing the firmware app keeps the settings; erasing flash removes them.

The Wi-Fi password and data key are stored unencrypted on the panel (see README, "Security"). They are never
printed, logged, or put in any build output. The temporary files this tool makes are deleted when it finishes.
"""
import argparse
import configparser
import csv
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = ROOT / "config.ini"
NVS_OFFSET = "0x9000"        # firmware/partitions.csv
NVS_SIZE = "0x6000"
API_BASE = "https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepartureBoard/"
USER_AGENT = "train-info-panel/0.1 (personal departure board)"   # the gateway rejects generic client agents
MAX_DIRECTIONS = 5
QUOTA_WARNING = 90_000       # requests a month; the free tier is believed to be ~100,000

PLACEHOLDERS = ("YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD", "YOUR_RAIL_DATA_MARKETPLACE_KEY")


class ConfigError(Exception):
    """A problem in config.ini, worded for the person editing it."""


# name in config.ini -> (section, option, type, default, low, high, NVS key, scale to stored units)
NUMBERS = [
    ("polling", "visible_interval_seconds", int, 60, 30, 3600, "vis_int", 1),
    ("polling", "background_interval_seconds", int, 240, 60, 86400, "bg_int", 1),
    ("polling", "request_timeout_seconds", int, 10, 3, 30, "req_to", 1),
    ("polling", "rows", int, 8, 4, 10, "rows", 1),
    ("warnings", "stale_after_minutes", int, 10, 2, 60, "stale_s", 60),
    ("warnings", "show_age_after_seconds", int, 120, 30, 3600, "age_s", 1),
    ("warnings", "alert_seconds_after_touch", float, 5, 0, 60, "tk_touch_ms", 1000),
    ("warnings", "alert_seconds_on_update", float, 15, 0, 60, "tk_upd_ms", 1000),
    ("warnings", "alert_delay_minutes", int, 10, 1, 120, "tk_late", 1),
    ("quiet_hours", "start_hour", int, 21, 0, 23, "q_start", 1),
    ("quiet_hours", "end_hour", int, 6, 0, 23, "q_end", 1),
    ("quiet_hours", "wake_seconds", int, 120, 10, 3600, "awake_s", 1),
    ("touch", "swipe_distance_pixels", int, 70, 20, 250, "swp_px", 1),
    ("touch", "swipe_time_ms", int, 900, 200, 3000, "swp_ms", 1),
]
TRUE = {"1", "yes", "true", "on"}
FALSE = {"0", "no", "false", "off"}


def read_ini(path):
    if not Path(path).exists():
        raise ConfigError(f"{path} not found. Copy config.example.ini to config.ini and edit it.")
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None, delimiters=("=",))
    cp.optionxform = str.lower
    try:
        cp.read(path, encoding="utf-8")
    except configparser.Error as e:
        raise ConfigError(f"{path} is not a valid config file: {e}") from None
    return cp


def _get(cp, section, option, default=None):
    if cp.has_option(section, option):
        v = cp.get(section, option).strip()
        return v
    return default


def _need(cp, section, option, what):
    v = _get(cp, section, option, "")
    if not v:
        raise ConfigError(f"[{section}] {option} is required ({what}).")
    if v in PLACEHOLDERS:
        raise ConfigError(f"[{section}] {option} is still the placeholder text '{v}'. Put your own value there.")
    return v


def _crs(value, where):
    v = value.strip().upper()
    if not re.fullmatch(r"[A-Z]{3}", v):
        raise ConfigError(f"{where} must be a 3-letter station code like BKM, not '{value}'.")
    return v


def _ascii(value, where, limit):
    if not value or any(ord(c) < 0x20 or ord(c) > 0x7E for c in value):
        raise ConfigError(f"{where} must be plain text (letters, digits, punctuation), not '{value}'.")
    if len(value.encode()) > limit:
        raise ConfigError(f"{where} is too long ({len(value)} characters; the limit is {limit}).")
    return value


def parse_config(cp):
    """config.ini -> a validated dict: {'strings': {nvs_key: str}, 'numbers': {nvs_key: int}, 'summary': {...}}."""
    strings, numbers = {}, {}

    ssid = _need(cp, "wifi", "ssid", "the 2.4 GHz network the panel should join")
    if len(ssid.encode()) > 32:
        raise ConfigError("[wifi] ssid is longer than 32 bytes, the Wi-Fi limit.")
    pw = _need(cp, "wifi", "password", "the Wi-Fi password")
    if not (8 <= len(pw.encode()) <= 64):
        raise ConfigError("[wifi] password must be 8 to 64 characters (WPA2). Open networks are not supported.")
    key = _need(cp, "api", "key", "your Rail Data Marketplace consumer key")
    if len(key.encode()) > 95 or re.search(r"\s", key):
        raise ConfigError("[api] key does not look right: it should be one string with no spaces.")
    strings.update(wifi_ssid=ssid, wifi_pass=pw, api_key=key)

    crs = _crs(_get(cp, "station", "crs", "BKM"), "[station] crs")
    name = _ascii(_get(cp, "station", "name", "") or crs, "[station] name", 23)
    strings.update(station=crs, name=name)

    directions = []
    for i in range(1, MAX_DIRECTIONS + 1):
        sec = f"direction.{i}"
        if not cp.has_section(sec):
            if any(cp.has_section(f"direction.{j}") for j in range(i + 1, MAX_DIRECTIONS + 2)):
                raise ConfigError(f"Directions must be numbered without gaps: [direction.{i}] is missing.")
            break
        label = _ascii(_get(cp, sec, "label", ""), f"[{sec}] label", 16)
        toward = _get(cp, sec, "toward", "")
        toward = _crs(toward, f"[{sec}] toward") if toward else ""
        directions.append((label, toward))
    if cp.has_section(f"direction.{MAX_DIRECTIONS + 1}"):
        raise ConfigError(f"At most {MAX_DIRECTIONS} directions are supported.")
    if not directions:
        raise ConfigError("Add at least one direction: a [direction.1] section with a label.")
    for i, (label, toward) in enumerate(directions, 1):
        strings[f"d{i}_label"] = label
        if toward:
            strings[f"d{i}_to"] = toward
        if toward == crs:
            raise ConfigError(f"[direction.{i}] toward is the station itself ({crs}); pick a different station.")

    for section, option, typ, default, lo, hi, nvs_key, scale in NUMBERS:
        raw = _get(cp, section, option)
        if raw is None or raw == "":
            val = default
        else:
            try:
                val = typ(raw)
            except ValueError:
                raise ConfigError(f"[{section}] {option} must be a number, not '{raw}'.") from None
        if not (lo <= val <= hi) or (isinstance(val, float) and math.isnan(val)):
            raise ConfigError(f"[{section}] {option} must be between {lo} and {hi}, not {raw if raw else val}.")
        numbers[nvs_key] = int(round(val * scale))

    q = (_get(cp, "quiet_hours", "enabled", "yes") or "yes").lower()
    if q not in TRUE | FALSE:
        raise ConfigError(f"[quiet_hours] enabled must be yes or no, not '{q}'.")
    numbers["q_on"] = 1 if q in TRUE else 0
    tz = _get(cp, "quiet_hours", "timezone", "GMT0BST,M3.5.0/1,M10.5.0") or "GMT0BST,M3.5.0/1,M10.5.0"
    strings["tz"] = _ascii(tz, "[quiet_hours] timezone", 47)

    if numbers["age_s"] > numbers["stale_s"]:
        raise ConfigError("[warnings] show_age_after_seconds must not be longer than stale_after_minutes.")

    return {"strings": strings, "numbers": numbers, "directions": directions, "crs": crs}


def monthly_requests(numbers, n_directions):
    """Rough requests a month, for the quota warning (touch wake-ups and swipes add a few)."""
    active_hours = 24.0
    if numbers["q_on"]:
        span = (numbers["q_end"] - numbers["q_start"]) % 24
        active_hours = 24.0 - span
    per_hour = 3600.0 / numbers["vis_int"] + max(n_directions - 1, 0) * 3600.0 / numbers["bg_int"]
    return int(per_hour * active_hours * 30)


NVS_KEYS_STRING = ("wifi_ssid", "wifi_pass", "api_key", "station", "name", "tz")


def nvs_rows(cfg):
    rows = [["panel", "namespace", "", ""]]
    for k, v in cfg["strings"].items():
        rows.append([k, "data", "string", v])
    for k, v in cfg["numbers"].items():
        rows.append([k, "data", "u32", str(v)])
    return rows


def build_nvs_image(cfg, workdir):
    csv_path, bin_path = workdir / "nvs.csv", workdir / "nvs.bin"
    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["key", "type", "encoding", "value"])
        w.writerows(nvs_rows(cfg))
    os.chmod(csv_path, 0o600)
    idf_gen = Path(os.environ.get("IDF_PATH") or "/nonexistent") / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    for cmd in ([sys.executable, "-m", "esp_idf_nvs_partition_gen"], [sys.executable, str(idf_gen)]):
        if cmd[-1].endswith(".py") and not idf_gen.exists():
            continue
        r = subprocess.run(cmd + ["generate", str(csv_path), str(bin_path), NVS_SIZE], capture_output=True, text=True)
        if r.returncode == 0 and bin_path.exists():
            return bin_path
        if "No module named" not in r.stderr:
            raise SystemExit("could not build the settings image:\n" + r.stderr.strip()[-400:])
    raise SystemExit("missing a helper tool. Run:  pip install -r tools/requirements.txt   (or use an ESP-IDF shell)")


def esptool(args):
    r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c6"] + args)
    if r.returncode != 0:
        raise SystemExit("flashing failed. Is the panel plugged in? Try another USB cable or port (--port), or hold BOOT while plugging in.")


def check_firmware_image(path):
    """--firmware must be the MERGED release image (bootloader + partition table + app, written at 0x0).
    A bare app image written there erases the bootloader and the board will not boot, so refuse it."""
    data = Path(path).read_bytes()[:0x8002]
    if len(data) < 0x8002 or data[0] != 0xE9 or data[0x8000:0x8002] != b"\xaa\x50":
        sys.exit(f"{path} is not a merged firmware image (no partition table at 0x8000), so flashing it at 0x0 would "
                 "break the bootloader. Use the train-info-panel-<version>.bin from the Releases page, or build one with "
                 "tools/package_release.py. (A bare firmware/build/train-info-panel.bin is not the right file.)")


def test_api(cfg):
    """One real request per direction. Prints results; never prints the key."""
    key, station = cfg["strings"]["api_key"], cfg["crs"]
    ok = True
    for label, toward in cfg["directions"]:
        url = f"{API_BASE}{station}?numRows=10" + (f"&filterCrs={toward}&filterType=to" if toward else "")
        req = urllib.request.Request(url, headers={"x-apikey": key, "User-Agent": USER_AGENT, "Accept": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = json.load(resp)
        except urllib.error.HTTPError as e:
            hint = " (is the key right, and subscribed to the LDBWS Public product?)" if e.code in (401, 403) else ""
            print(f"  {label}: the data service answered HTTP {e.code}{hint}")
            ok = False
            continue
        except (OSError, ValueError) as e:
            print(f"  {label}: could not reach the data service ({e.__class__.__name__}). Check your internet connection.")
            ok = False
            continue
        name = data.get("locationName")
        n = len(data.get("trainServices") or [])
        if data.get("areServicesAvailable") is False:
            print(f"  {label}: {name}: the station reports no services right now")
        elif not name:
            print(f"  {label}: unexpected answer; is {station} a valid station code?")
            ok = False
        else:
            tail = "" if n else "  (none in the next 2 hours; at night that is normal, otherwise check the station codes)"
            print(f"  {label}: {name}{' -> ' + toward if toward else ''}: {n} train(s) due{tail}")
    return ok


def main():
    ap = argparse.ArgumentParser(description="Write the panel's settings from config.ini (see the file's comments).")
    ap.add_argument("--config", default=str(DEFAULT_CONFIG), help="path to config.ini (default: config.ini in the repo root)")
    ap.add_argument("--port", default="/dev/ttyACM0", help="the panel's serial port (Windows: COM3 etc.; macOS: /dev/cu.usbmodem...)")
    ap.add_argument("--firmware", metavar="FILE", help="flash this firmware image first (a release .bin), then the settings")
    ap.add_argument("--check", action="store_true", help="validate config.ini and test the data key online; flash nothing")
    ap.add_argument("--dry-run", action="store_true", help="validate and build the settings image offline; flash nothing")
    a = ap.parse_args()

    try:
        cfg = parse_config(read_ini(a.config))
    except ConfigError as e:
        sys.exit(f"config problem: {e}")

    est = monthly_requests(cfg["numbers"], len(cfg["directions"]))
    print(f"config OK: station {cfg['crs']} ({cfg['strings']['name']}), {len(cfg['directions'])} direction(s), Wi-Fi '{cfg['strings']['wifi_ssid']}'")
    print(f"estimated data requests: about {est:,} a month" + ("" if est <= QUOTA_WARNING else "  <-- WARNING: the free tier is believed to be ~100,000. Poll less often or lengthen quiet hours."))

    if a.check:
        print("testing the data key against the live service:")
        sys.exit(0 if test_api(cfg) else 1)

    work = Path(tempfile.mkdtemp(prefix="panel-nvs-"))
    os.chmod(work, 0o700)
    try:
        image = build_nvs_image(cfg, work)
        if a.dry_run:
            print("dry run: settings image built and checked; nothing flashed")
            return
        if a.firmware:
            if not Path(a.firmware).exists():
                sys.exit(f"firmware file not found: {a.firmware}")
            check_firmware_image(a.firmware)
            print("flashing firmware...")
            esptool(["-p", a.port, "write_flash", "0x0", a.firmware])
        print("writing settings...")
        esptool(["-p", a.port, "write_flash", NVS_OFFSET, str(image)])
        print("done. The panel restarts by itself (if it does not, unplug and replug it) and should show data within a minute.")
    finally:
        shutil.rmtree(work, ignore_errors=True)   # the temporary CSV holds the secrets in clear: always remove it


if __name__ == "__main__":
    main()
