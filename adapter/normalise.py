"""Normalise an LDBWS station board (RDM JSON) into the panel payload (docs/payload.md).

Provider-specific knowledge lives here and nowhere in the firmware.
Field names follow the LDBWS JSON shape; verify against the RDM OpenAPI spec once a key exists.
"""
import html
import re
from datetime import datetime
from zoneinfo import ZoneInfo

LONDON = ZoneInfo("Europe/London")
MAX_SERVICES = 6
MAX_REASON = 60
MAX_MESSAGE = 80


class Unavailable(Exception):
    """Upstream answered but has no usable board (caller should fall back, not claim live)."""


def _clip(text, n):
    if not text:
        return None
    text = " ".join(str(text).split())
    return text if len(text) <= n else text[: n - 1].rstrip() + "…"


def _minutes(hhmm):
    h, m = hhmm.split(":")
    return int(h) * 60 + int(m)


def _late(std, etd):
    """Minutes late, tolerant of midnight wrap."""
    d = _minutes(etd) - _minutes(std)
    if d < -720:
        d += 1440
    return d


def _message_text(m):
    """nrccMessages items are {"Value": "<p>…<a href>…</a></p>"} per the spec; tolerate bare strings."""
    if isinstance(m, dict):
        m = m.get("Value") or m.get("value")
    if not isinstance(m, str):
        return None
    return html.unescape(re.sub(r"<[^>]*>", " ", m))


def _parse_time(value):
    value = (value or "").rstrip("*").strip()
    if len(value) == 5 and value[2] == ":" and value[:2].isdigit() and value[3:].isdigit():
        return value
    return None


def _service(raw, prev_platforms, flt_name=None):
    std = _parse_time(raw.get("std"))
    if std is None:
        return None
    dest = (raw.get("destination") or [{}])[0]
    etd_raw = (raw.get("etd") or "").rstrip("*").strip()
    etd, late = None, 0
    if raw.get("isCancelled") or etd_raw == "Cancelled":
        state, why = "cancelled", raw.get("cancelReason")
    elif raw.get("filterLocationCancelled"):
        # Still runs, but no longer calls at the station we filter on: cancelled for this board.
        state, why = "cancelled", f"Not stopping at {flt_name}" if flt_name else "Not stopping here"
    elif etd_raw == "On time":
        # Darwin may say "On time" from the schedule alone; the board cannot tell us otherwise.
        state, etd, why = "on_time", std, None
    elif etd_raw == "Delayed":
        state, why = "delayed", raw.get("delayReason")
    elif _parse_time(etd_raw):
        etd = etd_raw
        late = max(_late(std, etd), 0)
        state = "delayed" if late > 0 else "on_time"
        why = raw.get("delayReason") if late > 0 else None
    else:
        state, why = "unknown", None
    plat = (raw.get("platform") or "").strip() or None
    was = prev_platforms.get(raw.get("serviceID"))
    return {
        "std": std,
        "etd": etd,
        "state": state,
        "late": late,
        "plat": plat,
        "plat_was": was if (was and plat and was != plat) else None,
        "dest": dest.get("locationName") or "Unknown",
        "dest_crs": dest.get("crs"),
        "via": _clip(dest.get("via"), 40),
        "op": raw.get("operatorCode"),
        "why": _clip(why, MAX_REASON),
    }


def normalise(board, flt=None, prev_platforms=None):
    """board: parsed LDBWS JSON. flt: {"mode","crs","name"} the adapter asked for, or None."""
    prev_platforms = prev_platforms or {}
    # areServicesAvailable=false means the station is not offering services at all (outage/closure).
    # A missing or empty trainServices with it true is a genuine, live "no trains in the window".
    if board.get("areServicesAvailable") is False:
        raise Unavailable("board has no services available")
    flt_name = (flt or {}).get("name")
    services = [s for s in (_service(r, prev_platforms, flt_name) for r in (board.get("trainServices") or [])) if s]
    generated = datetime.fromisoformat(board["generatedAt"])
    if generated.tzinfo is None:
        generated = generated.replace(tzinfo=LONDON)
    messages = [m for m in (_clip(_message_text(m), MAX_MESSAGE) for m in (board.get("nrccMessages") or [])) if m]
    return {
        "v": 1,
        "mode": "live",
        "generated": int(generated.timestamp()),
        "station": {"crs": board["crs"], "name": board["locationName"]},
        "filter": flt,
        "message": messages[0] if messages else None,
        "services": services[:MAX_SERVICES],
    }
