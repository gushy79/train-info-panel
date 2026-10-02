#pragma once
/*
 * Display bring-up for the JD9853 panel: SPI bus, panel handle, and backlight
 * PWM. Adapted from Waveshare's official ESP32-C6-Touch-LCD-1.47 demo package
 * (ESP-IDF/03_lvgl_example) -- restructured to this project's own layout, but
 * the pin values, panel config, and rotation gap are the board's own facts,
 * not a creative choice, so they match the vendor example.
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the SPI bus. Call once, before bsp_display_init(). */
void bsp_spi_init(void);

/* Bring up the JD9853 panel over the already-initialised SPI bus, rotated into
 * this project's fixed 320x172 landscape orientation. */
void bsp_display_init(esp_lcd_panel_io_handle_t *io_handle,
                       esp_lcd_panel_handle_t *panel_handle,
                       size_t max_transfer_sz);

/* Whole-screen power: off = backlight 0 and the panel's display-off command; on = the reverse. */
void bsp_display_set_power(bool on);

void bsp_display_brightness_init(void);
void bsp_display_set_brightness(uint8_t brightness_percent);

#ifdef __cplusplus
}
#endif
