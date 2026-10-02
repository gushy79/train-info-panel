# Firmware toolchain decision

**ESP-IDF v5.3.2 + LVGL 8.4 (`esp_lvgl_port`).** Decided 2026-10-02.

## Why

- Espressif's own framework for the ESP32-C6, with first-class support for the board's native USB Serial/JTAG (flashing and recovery need no extra hardware) and Wi-Fi/TLS, which this project depends on.
- Waveshare's official demo package for this board is ESP-IDF + LVGL, so the display bring-up (`components/esp_lcd_jd9853`, `main/display/`) was derived from known-good code rather than rediscovered.
- LVGL 8.4 is mature and small enough for a 512 KB-RAM chip with no PSRAM (48 KB heap configured).
- Reproducible: `firmware/dependencies.lock` pins the component versions.

## Approach

- Copy (not symlink) the display pieces into `firmware/` so this repo builds on its own; provenance is recorded in `firmware/components/esp_lcd_jd9853/README.md` and the README's notices section.
- Build/flash: see the quick start in the top-level `README.md` and `firmware/README.md`. `dependencies.lock` is committed so component versions are reproducible.
- Development order that worked: fixture screens on the real panel first (`CONFIG_TRAIN_DEMO_CYCLE` keeps that mode), then networking, then touch, then quiet hours.
- Fonts: built-in Montserrat (12-24 px and 48 px enabled in `sdkconfig.defaults`). It is regular weight and ASCII-only for our purposes (see `docs/hardware-notes.md`). A bold, tabular-numeral font would be a later refinement.
- Logic that must be right (parsing, scheduling, takeover, swipes, quiet hours) lives in `firmware/main/board/` with no ESP-IDF dependency, so it is tested on the host under ASan/UBSan (`firmware/test/host/run.sh`).
