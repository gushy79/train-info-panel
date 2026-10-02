#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Minimal AXS5106L reader: first touch point only, in the panel's native portrait coordinates
 * (x 0..171, y 0..319). Protocol (from Waveshare's published demo): write register 0x01, read 14
 * bytes; byte 1 low nibble = number of points; point n has X high nibble at [2+6n], X low at
 * [3+6n], Y high nibble at [4+6n], Y low at [5+6n]. */
esp_err_t axs5106_init(void);

/* True and fills x,y while a finger is down; false when no touch or the read failed. */
bool axs5106_read(uint16_t *x, uint16_t *y);

/* Diagnostics: interrupt edges seen, reads that succeeded, reads that were NACKed. */
void axs5106_stats(uint32_t *int_edges, uint32_t *reads_ok, uint32_t *reads_nack);
