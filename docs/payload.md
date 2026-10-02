# Panel payload v1

The compact, provider-neutral board model. It is the shared contract between the fixtures (`testdata/payload/`), the Python reference normaliser (`adapter/normalise.py`) and the firmware's `board_t` (`firmware/main/board/board_logic.h`, filled by `ldbws_parse.c`). The panel does not receive this JSON over the network (it parses LDBWS itself, `docs/architecture-decision.md`); the JSON form exists so fixtures, tests and design renders all use one definition. A six-service board is ~1.5 KB. Fixtures: `testdata/payload/`. Reference validator: `validate()` in `adapter/test_contract.py`; the firmware parser must enforce the same rules and reject anything else.

```json
{
  "v": 1,
  "mode": "live",
  "generated": 1790925012,
  "station": {"crs": "BKM", "name": "Berkhamsted"},
  "filter": {"mode": "to", "crs": "HML", "name": "Hemel Hempstead", "label": "Southbound"},
  "message": null,
  "services": [
    {"std": "08:14", "etd": "08:21", "state": "delayed", "late": 7,
     "plat": "1", "plat_was": null, "dest": "London Euston", "dest_crs": "EUS",
     "via": null, "op": "GW", "why": "a signalling problem"}
  ]
}
```

| Field | Rule |
|---|---|
| `v` | Must be `1`; otherwise reject. |
| `mode` | `live`: realtime board fetched from LDBWS. `scheduled`: timetable only; every service has `state:"unknown"`, `etd`/`plat` null. |
| `generated` | Unix seconds, when the *provider* produced the board (not when the panel fetched it). |
| `filter` | `null` = all trains; else `mode` `to`/`from` with CRS, display `name`, and a header `label` (≤16 chars: `Southbound`, `Northbound`, `To Euston`). The panel shows the label, since a direction's trains have varied destinations. |
| `message` | One provider notice, ≤80 chars, or null. |
| `services` | ≤6, ordered by `std`. May be **empty** on a live board: nothing expected in the provider's window (default 2 h). That is a real answer, not an error. `std`/`etd` are local `HH:MM`. |
| `state` | `on_time`, `delayed`, `cancelled`, `unknown`. `etd` is null when cancelled, or when delayed with no time given. |
| `late` | Whole minutes late (≥0). Presentation decides what is *material*. |
| `plat` / `plat_was` | Current platform or null; `plat_was` set only when it differs from the previous poll. |
| `dest` | Full destination name, never pre-truncated; the firmware owns truncation. `via` ≤40 chars. |
| `why` | Cancellation/delay reason, ≤60 chars. |

## Freshness is the device's judgement

The panel computes age as `now - generated` (NTP time) and shows it. Proposed thresholds, to confirm in the concept work:

| Age | Treatment |
|---|---|
| < 2 min | live |
| 2–10 min | live, with visible age |
| > 10 min | stale: visibly different, no countdowns, scheduled times only |
| fetch failed / malformed / `v` unknown | keep last-good with age; never promote to live |

A `mode: scheduled` payload is never shown as live regardless of age. "No network" is the absence of a response, so it has no fixture; `stale.json` (25 min old) and `malformed.json` (truncated JSON) cover the other failure cases.
