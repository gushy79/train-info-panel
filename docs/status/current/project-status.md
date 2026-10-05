# Train Info Panel - Project Status

**Last updated:** 2026-10-03
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

- Settings moved from compile-time constants to runtime NVS config; verified on the panel (boot log shows them).
- Alert length split: `alert_seconds_after_touch` (5) vs `alert_seconds_on_update` (15).
- Neutral example station and synthetic fixtures; docs and history cleaned for public release.
- Apache-2.0 licence added; repo published; v1.0.0 release (image + SHA-256) built from a fresh checkout and
  verified (anonymous download, checksum, image structure, no secrets or build paths in the binary).
- Overnight behaviour confirmed on the panel: screen off after quiet hours begin, wakes on touch, on again in the morning.

## Decisions (see docs/)

LDBWS via Rail Data Marketplace, called by the panel directly (no home server); directions are "toward the next
station"; ESP-IDF + LVGL; flashing is documented with Espressif's existing tools (Flash Download Tool, ESP
Launchpad, esptool), then `provision.py` writes settings (flash first: the image blanks the settings area).
Rejected: a custom web flasher and native apps (browser limits, signing, upkeep).

## Backlog

1. **Soak test** (several days): Wi-Fi recovery, whether a neighbouring-station filter ever misses a train, real
   delay and cancellation behaviour (the two alert lengths have not yet been seen on a real event).
2. **Confirm the API quota** on the RDM subscription page (believed ~100,000/month; default use ~40,000).
3. **Timetable fallback** (low priority): the screen is designed and renders from fixtures, but is not reachable.
4. Ideas: read `config.ini` from the board's microSD slot (would remove the Python step; needs firmware work and
   hardware testing); "stops at X" filter via calling points; bold/tabular font; tap actions.
5. The exact v1.0.0 image has not been flashed to a panel (same code as the verified build; only example defaults
   and the version label differ).
6. **Settings screen and saved presets** (feature, needs a design note in `docs/` first). Swiping past the last
   direction reaches a full-screen "Settings" button. A swipe moves on as normal; a tap enters settings.
   - Settings is a short series of screens, swiped through, with one or two large buttons on each.
   - **Mode / station presets:** pick from stations saved in `config.ini` (e.g. departures or arrivals at the home
     station, or other stations visited regularly) without reprovisioning. Adds an *arrivals* board mode (LDBWS
     arrivals endpoint), which the firmware does not use yet.
   - **Route presets:** named origin-to-destination routes (e.g. "A to B", "B to C") that show only trains
     calling at both. Needs a calling-points filter (overlaps with the "stops at X" idea in item 4).
   - Open questions: where preset lists live (`config.ini` to NVS, so no on-device text entry); whether the
     selection survives a reboot; per-preset cache and API-quota cost (the `--check` estimate must cover it);
     how the settings screen interacts with quiet hours and alerts; touch targets at 320x172.
7. **Upcoming-trains list and single-train tracking** (feature). Fetch a longer window of departures (about the next
   two hours) for the current station or preset, scroll through them vertically, and tap one to track it.
   - Tracking shows that one train in a focused overlay-style view (time, destination, platform, status), polled
     more closely than the normal board, and returns to the board on a timeout or tap.
   - Open questions: LDBWS row and time-window limits and the extra request cost; whether tracking needs the
     service-details endpoint; a vertical scroll must not clash with the horizontal direction swipe; this goes
     against the "no continuous scrolling" brief, so keep it tap-initiated and static; the tracked train must
     say plainly when it departs, is cancelled or drops off the board.
   - Items 6 and 7 both want a tap-driven UI, so settle shared touch conventions (tap, swipe, back, timeout) once.

## Maintainer actions

- Watch the panel for a few days and report anything odd. Keep a private backup of `config.ini` (holds secrets).
