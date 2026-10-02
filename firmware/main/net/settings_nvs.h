#pragma once
/* Fill the live settings (board/settings.h) from the device's NVS, falling back to compiled-in defaults
 * for anything absent. NVS is written by tools/provision.py from the user's config.ini. Call once at
 * boot after nvs_flash_init(), before other tasks start. Logs the effective values (never secrets). */
void settings_load_from_nvs(void);
