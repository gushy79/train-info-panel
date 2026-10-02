#include "net/settings_nvs.h"

#include <stdio.h>
#include <string.h>

#include "board/settings.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

static void get_str(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    char tmp[64];
    size_t len = sizeof tmp;
    if (nvs_get_str(h, key, tmp, &len) != ESP_OK || !tmp[0]) return;   /* absent or empty: keep the default */
    snprintf(out, cap, "%s", tmp);
}

static bool get_u32(nvs_handle_t h, const char *key, uint32_t *out)
{
    uint32_t v;
    if (nvs_get_u32(h, key, &v) != ESP_OK) return false;
    *out = v;
    return true;
}

/* Narrow types: take the stored value if present, saturating so the sanitiser sees a too-big number
 * as too big instead of a wrapped small one. */
static void get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    uint32_t v;
    if (get_u32(h, key, &v)) *out = v > 255 ? 255 : (uint8_t)v;
}

static void get_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    uint32_t v;
    if (get_u32(h, key, &v)) *out = v > 65535 ? 65535 : (uint16_t)v;
}

void settings_load_from_nvs(void)
{
    settings_t s;
    settings_set_defaults(&s);

    nvs_handle_t h;
    if (nvs_open("panel", NVS_READONLY, &h) == ESP_OK) {
        get_str(h, "station", s.station_crs, sizeof s.station_crs);
        get_str(h, "name", s.station_name, sizeof s.station_name);
        get_str(h, "tz", s.timezone, sizeof s.timezone);

        /* Directions d1..d5, consecutive; none present = keep the defaults. */
        int count = 0;
        direction_cfg_t dirs[MAX_DIRECTIONS];
        memset(dirs, 0, sizeof dirs);
        for (int i = 0; i < MAX_DIRECTIONS; i++) {
            char key[12];
            snprintf(key, sizeof key, "d%d_label", i + 1);
            get_str(h, key, dirs[i].label, sizeof dirs[i].label);
            if (!dirs[i].label[0]) break;
            snprintf(key, sizeof key, "d%d_to", i + 1);
            get_str(h, key, dirs[i].toward, sizeof dirs[i].toward);   /* absent = all trains */
            count++;
        }
        if (count > 0) {
            memcpy(s.directions, dirs, sizeof dirs);
            s.direction_count = (uint8_t)count;
        }

        get_u8(h, "rows", &s.num_rows);
        get_u32(h, "vis_int", &s.visible_interval_s);
        get_u32(h, "bg_int", &s.background_interval_s);
        get_u32(h, "req_to", &s.request_timeout_s);
        get_u32(h, "stale_s", &s.stale_after_s);
        get_u32(h, "age_s", &s.age_shown_after_s);
        get_u32(h, "tk_touch_ms", &s.alert_touch_ms);
        get_u32(h, "tk_upd_ms", &s.alert_update_ms);
        get_u8(h, "tk_late", &s.takeover_min_late_min);
        uint32_t q = s.quiet_enabled;
        get_u32(h, "q_on", &q);
        s.quiet_enabled = q != 0;
        get_u8(h, "q_start", &s.quiet_start_hour);
        get_u8(h, "q_end", &s.quiet_end_hour);
        get_u32(h, "awake_s", &s.awake_after_touch_s);
        get_u16(h, "swp_px", &s.swipe_min_px);
        get_u16(h, "swp_ms", &s.swipe_max_ms);
        nvs_close(h);
    }

    int fixed = settings_sanitise(&s);
    if (fixed) ESP_LOGW(TAG, "%d config value(s) were out of range and have been adjusted", fixed);
    settings_install(&s);

    ESP_LOGI(TAG, "station %s (%s), %u direction(s), default \"%s\"", s.station_crs, s.station_name,
             (unsigned)s.direction_count, s.directions[0].label);
    for (int i = 0; i < s.direction_count; i++)
        ESP_LOGI(TAG, "  direction %d: %s -> %s", i + 1, s.directions[i].label,
                 s.directions[i].toward[0] ? s.directions[i].toward : "(all trains)");
    ESP_LOGI(TAG, "polling %us on screen / %us background, stale after %us, alerts at >= %u min late (%ums after a touch, %ums on update)",
             (unsigned)s.visible_interval_s, (unsigned)s.background_interval_s, (unsigned)s.stale_after_s,
             (unsigned)s.takeover_min_late_min, (unsigned)s.alert_touch_ms, (unsigned)s.alert_update_ms);
    if (s.quiet_enabled)
        ESP_LOGI(TAG, "quiet hours %02u:00-%02u:00, touch keeps the screen on for %us", s.quiet_start_hour,
                 s.quiet_end_hour, (unsigned)s.awake_after_touch_s);
    else
        ESP_LOGI(TAG, "quiet hours off");
}
