"""Regenerate testdata/payload/*.json. The contract test fails if committed files drift.

live/delayed/cancelled are derived from the raw LDBWS mocks through normalise().
scheduled, stale and malformed are hand-shaped: they model states the adapter or device
declares, which LDBWS itself cannot express.
"""
import json
from pathlib import Path

from normalise import normalise

ROOT = Path(__file__).resolve().parent.parent / "testdata"
FLT = {"mode": "to", "crs": "HML", "name": "Hemel Hempstead", "label": "Southbound"}  # what the first [direction] in config.example.ini produces


def raw(name):
    return json.loads((ROOT / "ldbws" / f"southbound_{name}.json").read_text())


def build():
    out = {
        "live_normal": normalise(raw("normal"), FLT),
        "delayed": normalise(raw("delayed"), FLT),
        # MOCK0002 was on platform 4 in the previous poll, now 3.
        "cancelled_platform_change": normalise(raw("cancelled"), FLT, {"MOCK0002": "4"}),
        "empty": normalise(raw("empty"), FLT),
    }
    sched = json.loads(json.dumps(out["live_normal"]))
    sched["mode"] = "scheduled"
    sched["message"] = None
    for s in sched["services"]:
        s.update(etd=None, state="unknown", late=0, plat=None, plat_was=None, why=None)
    out["scheduled_fallback"] = sched
    # Same content as live_normal; "stale" is the device's verdict from age, not a field.
    stale = json.loads(json.dumps(out["live_normal"]))
    stale["generated"] -= 25 * 60
    out["stale"] = stale
    return out


def main():
    for name, payload in build().items():
        (ROOT / "payload" / f"{name}.json").write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
    (ROOT / "payload" / "malformed.json").write_text('{"v": 1, "mode": "live", "generated": 17\n')


if __name__ == "__main__":
    main()
