# Hardware notes (Waveshare ESP32-C6-Touch-LCD-1.47)

Findings made on the real board, 2026-10-02. Pin table: https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47

## Touch controller (AXS5106L)

| Fact | Value | Note |
|---|---|---|
| I2C pins | SDA GPIO18, SCL GPIO19 | shared with the QMI8658A IMU (0x6B on the bus) |
| Reset / interrupt | GPIO20 / GPIO21 | interrupt is active low, falls once per touch report |
| **I2C address** | **0x63** | The wiki page lists 0x51, which NACKs. Waveshare's demo driver uses 0x63; a bus probe confirmed it. |
| Read protocol | write register `0x01`, then read 14 bytes **as two separate transactions with a STOP between** | A repeated-start read (`i2c_master_transmit_receive`) is NACKed every time, even while touched. |
| Report format | byte 1 low nibble = point count; point n: X = `[2+6n]&0x0F`<<8 \| `[3+6n]`, Y = `[4+6n]&0x0F`<<8 \| `[5+6n]` | native portrait coordinates, x 0..171, y 0..319 |
| Idle behaviour | NACKs reads while no finger is down | normal; the driver reads at full rate on the interrupt or while a finger is down, and every 100 ms otherwise (backstop) |
| Landscape mapping | screen x = native y, screen y = native x, no flip | verified by swiping: left/right behave as expected |

Presence is checked with an address probe at start-up (a data read would NACK while idle). If the controller is missing the panel still runs, without swipe.

## Display and USB

Display bring-up (`firmware/components/esp_lcd_jd9853`, `firmware/main/display`) is derived from Waveshare's demo package. Native USB Serial/JTAG is untouched, so BOOT-while-plugging-in always recovers the board.

## Fonts

Built-in LVGL Montserrat is regular weight and ASCII-only for our purposes: U+00B7 (middle dot), U+2026 (ellipsis) and U+2013 (en dash) render as boxes. All on-device strings are ASCII; the host test enforces it.
