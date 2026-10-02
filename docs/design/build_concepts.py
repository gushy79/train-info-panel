"""Emit docs/design/concepts.html: three 320x172 concepts x five fixture states, from testdata/payload.

Browser fonts (Liberation Sans) stand in for the device font; widths are indicative until
checked on the panel. Run: python3 docs/design/build_concepts.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAY = HERE.parent.parent / "testdata" / "payload"
STATES = ["live_normal", "delayed", "cancelled_platform_change", "scheduled_fallback", "stale", "empty"]
# Age of the data at display time, seconds (scheduled age is irrelevant to its mode).
AGE = {"empty": 20, "live_normal": 20, "delayed": 45, "cancelled_platform_change": 30, "scheduled_fallback": 0, "stale": 25 * 60}

data = {s: json.loads((PAY / f"{s}.json").read_text()) for s in STATES}
for s in STATES:
    data[s]["_age"] = AGE[s]

TEMPLATE = (HERE / "concepts.template.html").read_text()
(HERE / "concepts.html").write_text(TEMPLATE.replace("/*DATA*/null", json.dumps(data)))
print("wrote", HERE / "concepts.html")
