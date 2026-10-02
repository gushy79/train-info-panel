"""Fetch one live board from the real service and print it in the compact board model (docs/payload.md).

    python3 adapter/live_fetch.py                  # the first direction in config.ini
    python3 adapter/live_fetch.py --direction 2    # the second [direction.N] section
    python3 adapter/live_fetch.py --toward ZZZ     # any station, ignoring config.ini's directions
    python3 adapter/live_fetch.py --all            # every train, no direction filter

Station, directions and key come from config.ini (or $LDBWS_KEY for the key). The key is only ever sent as
the x-apikey header to --base; use --base http://127.0.0.1:8099/... to run against mock_ldbws.py instead.
Nothing here is a server: it fetches once and exits. A handy way to try station codes before flashing.
"""
import argparse
import configparser
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from normalise import normalise

DEFAULT_BASE = "https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepartureBoard/"
CONFIG = Path(__file__).resolve().parent.parent / "config.ini"


def load_config(path):
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None, delimiters=("=",))
    cp.optionxform = str.lower
    if Path(path).exists():
        cp.read(path, encoding="utf-8")
    return cp


def fetch(key, station, toward, base=DEFAULT_BASE, num_rows=8, timeout=10):
    query = {"numRows": num_rows}
    if toward:
        query.update(filterCrs=toward, filterType="to")
    req = urllib.request.Request(f"{base}{station}?{urllib.parse.urlencode(query)}",
                                 headers={"x-apikey": key, "User-Agent": "train-info-panel/0.1 (personal departure board)"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.load(resp)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--config", default=str(CONFIG))
    ap.add_argument("--station", help="override the station code")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--direction", type=int, default=1, help="which [direction.N] section of config.ini (default 1)")
    g.add_argument("--toward", metavar="CRS", help="a station the trains must call at")
    g.add_argument("--all", action="store_true", help="no direction filter")
    ap.add_argument("--base", default=DEFAULT_BASE)
    a = ap.parse_args()

    cp = load_config(a.config)
    key = os.environ.get("LDBWS_KEY") or cp.get("api", "key", fallback="").strip()
    if not key:
        sys.exit("no data key: set [api] key in config.ini (or $LDBWS_KEY)")
    station = (a.station or cp.get("station", "crs", fallback="")).strip().upper()
    if not station:
        sys.exit("no station: set [station] crs in config.ini, or pass --station")

    label, toward = "All trains", ""
    if a.toward:
        toward, label = a.toward.strip().upper(), f"To {a.toward.strip().upper()}"
    elif not a.all:
        sec = f"direction.{a.direction}"
        if not cp.has_section(sec):
            sys.exit(f"config.ini has no [{sec}] section")
        label = cp.get(sec, "label", fallback="").strip() or label
        toward = cp.get(sec, "toward", fallback="").strip().upper()

    try:
        board = fetch(key, station, toward, a.base)
    except urllib.error.HTTPError as e:
        sys.exit(f"upstream HTTP {e.code}")
    except OSError as e:
        sys.exit(f"upstream unreachable: {e.__class__.__name__}")
    flt = {"mode": "to", "crs": toward, "name": board.get("filterLocationName") or toward, "label": label} if toward else None
    json.dump(normalise(board, flt), sys.stdout, indent=2, ensure_ascii=False)
    print()


if __name__ == "__main__":
    main()
