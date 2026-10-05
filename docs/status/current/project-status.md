# Train Info Panel - Project Status

**Last updated:** 2026-10-05
**Phase:** v1.0.0 released (Apache-2.0). Running on hardware; field soak test in progress.

## What exists

- **Firmware** (`firmware/`): ESP-IDF 5.3.2 + LVGL 8.4. Joins Wi-Fi, syncs the clock, polls LDBWS directly, shows
  the board (two-line rows), full-screen alerts, empty and degraded states; swipe between directions with
  per-direction caches; quiet hours (screen off, no polling, touch wakes); two alert lengths (after a touch /
  on a background update).
- **Configuration:** one `config.ini` (from `config.example.ini`) holds every user setting. `tools/provision.py`
  validates it, can test the key and estimate API use (`--check`), and writes it to the panel's NVS. The firmware
  reads all behaviour values from NVS at runtime and clamps them to safe ranges; nothing user-adjustable is compiled in.
- **Release tooling:** `tools/package_release.py` builds one flashable image; `provision.py --firmware` refuses a
  non-merged image (a bare app image at 0x0 destroys the bootloader).
- **Reference tooling** (`adapter/`): Python normaliser (oracle for the C parser), mock LDBWS, live-fetch tool.
- **Tests:** `python3 -m unittest discover adapter` (13), `... discover tools` (15, incl. tool/firmware agreement
  on keys, ranges, defaults) and `firmware/test/host/run.sh` (parser vs fixtures, hostile input, settings, backoff,
  scheduling, alerts, swipes, quiet hours; ASan/UBSan). A fresh clone builds and passes all of them.
- **Docs:** README (user guide: key registration, config, flashing with existing tools, build from source),
  CONTRIBUTING (code map, tests, adding a setting), `docs/` decisions and hardware notes.

## Done (this session)

- Soak test so far: panel running without issues (no real disruption seen yet).
- Backlog extended: settings screen and presets (6), upcoming-trains list and tracking (7), per-day quiet hours (8),
  and a small immediate change of default quiet hours to 15:00-06:00.

## Decisions (see docs/)

LDBWS via Rail Data Marketplace, called by the panel directly (no home server); directions are "toward the next
station"; ESP-IDF + LVGL; flashing is documented with Espressif's existing tools (Flash Download Tool, ESP
Launchpad, esptool), then `provision.py` writes settings (flash first: the image blanks the settings area).
Rejected: a custom web flasher and native apps (browser limits, signing, upkeep).

## Backlog

**Next up (small):** change default quiet hours to 15:00-06:00 (now 21:00-06:00). Update `config.example.ini`, the
firmware NVS default, `tools/provision.py`, the README and the tool/firmware default-agreement test; recheck the
API-use estimate (fewer active hours, fewer requests).

1. **Soak test** (several days): Wi-Fi recovery, whether a neighbouring-station filter ever misses a train, real
   delay and cancellation behaviour (the two alert lengths have not yet been seen on a real event).
2. **Confirm the API quota** on the RDM subscription page (believed ~100,000/month; default use ~40,000).
3. **Timetable fallback** (low priority): the screen is designed and renders from fixtures, but is not reachable.
4. Ideas: read `config.ini` from the board's microSD slot (would remove the Python step); "stops at X" filter via
   calling points; bold/tabular font; tap actions.
5. The exact v1.0.0 image has not been flashed to a panel (same code as the verified build; only example defaults
   and the version label differ).
6. **Settings screen and presets** (needs a design note in `docs/` first). Swiping past the last direction reaches a
   full-screen "Settings" button: swipe moves on, tap enters a few swiped screens with one or two big buttons.
   Station presets (departures or arrivals; arrivals mode is new) and route presets (only trains calling at both
   ends; needs the calling-points filter from item 4). Open: presets live in `config.ini` to NVS (no on-device
   text entry); survive reboot?; per-preset polling and quota cost; interaction with quiet hours and alerts.
7. **Upcoming-trains list and single-train tracking.** Fetch ~2 hours of departures, scroll vertically, tap one to
   track it in a focused overlay polled more closely, returning on timeout or tap. Open: LDBWS limits, whether
   service details are needed, vertical scroll vs horizontal swipe, the brief's "no continuous scrolling" (keep it
   tap-initiated and static), plain wording when the train departs, is cancelled or drops off. Settle shared touch
   conventions (tap, swipe, back, timeout) for items 6 and 7 once.
8. **Per-day quiet hours:** a default plus optional per-day overrides; needs host tests for day-of-week and the
   midnight wrap (a window past midnight belongs to the day it starts on) and an updated `--check` estimate.

## Maintainer actions

- Watch the panel for a few days and report anything odd. Keep a private backup of `config.ini` (holds secrets).
