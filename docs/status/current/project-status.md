# Train Info Panel - Project Status

**Last updated:** 2026-10-02
**Phase:** v1 complete; reworked (2026-10-02) so every setting lives in one `config.ini`, ready for public use. **The new firmware has not yet been flashed to the panel** (it was unplugged); see Backlog item 1.

## What exists

- **Firmware** (`firmware/`): ESP-IDF 5.3.2 + LVGL 8.4. Joins Wi-Fi, syncs the clock, polls LDBWS directly, shows
  the board (concept C), takeover, empty and degraded states, swipe between the configured directions
  with per-direction caches, quiet hours (21:00-06:00: no polling, screen off, touch wakes for 2 minutes), alerts of two lengths (5 s after a touch, 15 s on a background update).
- **Configuration** (`config.ini`, from `config.example.ini`) and `tools/provision.py`: every setting, the Wi-Fi details and the key go into the board's NVS; no rebuild to change them.
- **Reference tooling** (`adapter/`): Python normaliser (oracle for the C parser), mock LDBWS, live-fetch tool.
- **Tests:** `python3 -m unittest discover adapter` (13), `python3 -m unittest discover tools` (14, including checks that the config tool and the firmware agree on keys, ranges and defaults) and `firmware/test/host/run.sh` (parser vs adapter
  fixtures, hostile input, backoff, scheduling, takeover timing, swipes, quiet hours; ASan/UBSan). A clean-clone
  build was verified before publishing.
- **Docs:** README (setup, flashing, recovery, status, limitations), `docs/` decisions and hardware notes.

## Verified on hardware

Live boards from the real service; board, delayed, cancelled, platform-change, stale and timetable renders;
swipe in both directions; instant direction switching; takeover length; quiet-hours screen-off and touch wake.
Not photographed: the empty-board screen and the cancelled takeover (render and log only).

## Decisions (see docs/)

LDBWS via Rail Data Marketplace; panel talks to it directly with key and Wi-Fi in its own NVS (no home server);
directions are "toward the neighbouring station"; concept C two-line rows; ESP-IDF + LVGL.

## Backlog

0. **Flash and verify on the panel** the settings-from-NVS build: `python3 tools/provision.py --firmware <image>` (or `idf.py flash` then `provision.py`), then check the serial log shows the `settings:` summary and the board loads. Also check the two alert lengths (5 s after a touch, 15 s on a background update). Host tests, tool tests and a clean-environment dry run all pass; the on-device read of the settings is the part not yet exercised.

1. **Soak test** (several days): Wi-Fi recovery, whether a neighbouring-station filter ever misses a train,
   real delay and cancellation behaviour, quiet-hours transitions across a night.
2. **Confirm the API quota** on the RDM subscription page (believed ~100,000/month; steady state ~40,000).
3. **Timetable fallback source** (low priority: the value is in live data). The screen is designed and
   renders from fixtures, but the live panel never enters it.
4. Optional: "stops at X" filter via calling points; bold/tabular font; tap actions.
5. Choose a licence before sharing the repository (none yet; all rights reserved by default).

## Maintainer actions

- Run it for a few days and report anything odd. Otherwise v1 is considered live.
