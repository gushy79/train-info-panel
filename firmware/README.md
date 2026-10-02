# firmware/

ESP-IDF v5.3.2 + LVGL 8.4 firmware for the Waveshare ESP32-C6-Touch-LCD-1.47 (landscape 320x172). This
file is for people **building from source**. If you only want to flash and configure a panel, follow the
top-level [`README.md`](../README.md): a release image plus `config.ini` is all you need.

Toolchain rationale: [`docs/firmware-toolchain.md`](../docs/firmware-toolchain.md).

## What it does

Joins Wi-Fi, syncs the clock, polls LDBWS (the direction on screen often, the others now and then, so a swipe
is instant), and draws the board, alerts and degraded states. All behaviour values (station, directions,
intervals, warning thresholds, quiet hours, swipe feel) are **runtime settings** loaded from the device's NVS,
which `tools/provision.py` fills from the user's `config.ini`. Nothing user-adjustable is compiled in; the
compiled defaults are only a fallback for values missing from NVS, and every value is clamped to a safe range.
The device listens on nothing: it only makes outbound requests (`docs/architecture-decision.md`).

Fixture-only demo (no Wi-Fi, cycles every designed screen): `idf.py menuconfig` -> *Train info panel* ->
*Cycle the fixture screens*.

## Layout

| Path | Purpose |
|---|---|
| `main/board/` | Logic with no ESP-IDF dependency (cJSON only), host-tested: settings, presentation rules, the LDBWS parser, backoff, scheduling, takeover timing, swipe detection, quiet hours. |
| `main/net/` | Wi-Fi, the HTTPS client, the NVS settings loader and the poll task. Outbound only. |
| `main/app/` | Chooses what to draw from the poller's snapshot; turns the screen off in quiet hours. |
| `main/touch/` | AXS5106L reader and the swipe task (hardware facts: `docs/hardware-notes.md`). |
| `main/ui/` | LVGL views: two-line board, takeover, empty, status. `theme.h` holds the palette. |
| `main/fixtures/` | **Generated** from `testdata/payload/*.json` by `tools/gen_fixtures_c.py`. Do not edit. |
| `main/spike/` | Fixture screen cycler (the demo option). |
| `main/display/`, `components/esp_lcd_jd9853/` | Display bring-up, derived from Waveshare's demo package (board wiring facts). |
| `test/host/` | Host tests (gcc, ASan/UBSan). |

## Build, flash, monitor

```sh
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf   # once
cd ~/esp/esp-idf && ./install.sh esp32c6                                                  # once

source ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32c6                 # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor      # Ctrl+] leaves the monitor
```

`idf.py flash` replaces the app but **leaves the settings** (they live in a separate partition). To flash a
fresh board and give it its settings in one go, from the repo root:

```sh
python3 tools/package_release.py                                    # builds dist/train-info-panel-<version>.bin
python3 tools/provision.py --firmware dist/train-info-panel-*.bin  # flashes it, then writes config.ini
```

`dependencies.lock` is committed so component versions are reproducible. `firmware/sdkconfig` is generated
and git-ignored.

## Tests

```sh
python3 firmware/tools/gen_fixtures_c.py   # only if testdata/payload changed
firmware/test/host/run.sh                  # needs gcc and ESP-IDF's bundled cJSON (IDF_PATH or ~/esp/esp-idf)
```

They cover the parser (cross-checked against the Python reference's fixtures), hostile and truncated input,
settings clamping and the way behaviour follows the settings, backoff, scheduling, takeover timing, swipe
detection and quiet hours, under ASan/UBSan.

On-device smoke test: after provisioning, the serial log should show `settings: station ...`, `connected`,
then `HTTP 200` and `board ok: N services` for each direction within about 20 seconds, with no `E (` lines.

## Recovery

Native USB Serial/JTAG is untouched: this firmware never configures GPIO8/9 or the USB pins, so a board that
will not boot or enumerate can always be brought back:

1. Hold **BOOT**, plug in USB (the board enumerates as `303a:1001`, usually `/dev/ttyACM0`).
2. `idf.py -p /dev/ttyACM0 erase-flash`, then flash the firmware again and re-run `tools/provision.py`
   (erasing flash removes the stored settings).

To go back to Waveshare's original firmware, use the factory image from their demo package at
<https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47> and `esptool write_flash 0x0 <image>`. Take a backup of a
new board first if you might want that: `esptool read_flash 0x0 0x800000 backup.bin`.

## Known limitations

- Montserrat (regular weight only, ASCII glyphs only: no middle dot, ellipsis or en dash): the design's bold
  weights and 60 px takeover figure are approximated (48 px is the largest built-in size).
- Touch is swipe/wake only (no tap actions). The backlight is on or off, no dimming.
- The last good board is held in RAM only; after a reboot the panel shows "Loading..." until its first fetch.
- The `TIMETABLE` (scheduled-fallback) screen is designed and renders from fixtures, but the live panel never
  enters it: there is no timetable source (`docs/data-source.md`).
