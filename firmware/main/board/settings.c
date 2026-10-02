#include "board/settings.h"

#include <ctype.h>
#include <string.h>

static settings_t g_settings;
static bool g_inited;

void settings_set_defaults(settings_t *s)
{
    memset(s, 0, sizeof *s);
    /* The author's own station, so an unconfigured panel still shows something sensible. */
    strcpy(s->station_crs, "BKM");
    strcpy(s->station_name, "Berkhamsted");
    s->direction_count = 3;
    strcpy(s->directions[0].label, "Southbound");  strcpy(s->directions[0].toward, "HML");
    strcpy(s->directions[1].label, "Northbound");  strcpy(s->directions[1].toward, "TRI");
    strcpy(s->directions[2].label, "All trains"); s->directions[2].toward[0] = '\0';
    s->num_rows = 8;

    s->visible_interval_s = 60;
    s->background_interval_s = 240;
    s->request_timeout_s = 10;

    s->stale_after_s = 600;
    s->age_shown_after_s = 120;
    s->alert_touch_ms = 5000;     /* you are looking: it only has to register */
    s->alert_update_ms = 15000;   /* you may be across the room: it must last long enough to be noticed */
    s->takeover_min_late_min = 10;

    s->quiet_enabled = true;
    s->quiet_start_hour = 21;
    s->quiet_end_hour = 6;
    s->awake_after_touch_s = 120;
    strcpy(s->timezone, "GMT0BST,M3.5.0/1,M10.5.0");   /* UK: BST from the last Sunday of March to the last of October */

    s->swipe_min_px = 70;
    s->swipe_max_ms = 900;
}

const settings_t *settings(void)
{
    if (!g_inited) { settings_set_defaults(&g_settings); g_inited = true; }
    return &g_settings;
}

void settings_install(const settings_t *s)
{
    g_settings = *s;
    g_inited = true;
}

/* ---- sanitising ------------------------------------------------------------------------------- */

static int clamp_u32(uint32_t *v, uint32_t lo, uint32_t hi)
{
    if (*v < lo) { *v = lo; return 1; }
    if (*v > hi) { *v = hi; return 1; }
    return 0;
}

static int clamp_u8(uint8_t *v, unsigned lo, unsigned hi)
{
    if (*v < lo) { *v = (uint8_t)lo; return 1; }
    if (*v > hi) { *v = (uint8_t)hi; return 1; }
    return 0;
}

static int clamp_u16(uint16_t *v, unsigned lo, unsigned hi)
{
    if (*v < lo) { *v = (uint16_t)lo; return 1; }
    if (*v > hi) { *v = (uint16_t)hi; return 1; }
    return 0;
}

/* Printable ASCII only (the panel font has nothing else); terminate; trim. Returns 1 if changed. */
static int clean_text(char *s, size_t cap, const char *fallback)
{
    int changed = 0;
    s[cap - 1] = '\0';
    for (char *p = s; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p >= 0x7F) { *p = '?'; changed = 1; }
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = '\0';
    if (!s[0] && fallback) { strncpy(s, fallback, cap - 1); changed = 1; }
    return changed;
}

/* A CRS code is exactly three capital letters. */
static int clean_crs(char *s, bool allow_empty, const char *fallback)
{
    s[3] = '\0';
    bool ok = strlen(s) == 3 && isupper((unsigned char)s[0]) && isupper((unsigned char)s[1]) && isupper((unsigned char)s[2]);
    if (ok || (allow_empty && !s[0])) return 0;
    strcpy(s, fallback);
    return 1;
}

int settings_sanitise(settings_t *s)
{
    int n = 0;
    n += clean_crs(s->station_crs, false, "BKM");
    n += clean_text(s->station_name, sizeof s->station_name, s->station_crs);

    n += clamp_u8(&s->direction_count, 1, MAX_DIRECTIONS);
    for (int i = 0; i < s->direction_count; i++) {
        char fallback[17];
        strncpy(fallback, i == 0 ? "Trains" : "Other", sizeof fallback - 1);
        fallback[sizeof fallback - 1] = '\0';
        n += clean_text(s->directions[i].label, sizeof s->directions[i].label, fallback);
        n += clean_crs(s->directions[i].toward, true, "");
    }
    n += clamp_u8(&s->num_rows, 4, 10);

    /* Floors protect the free API quota: nobody needs a board refreshed more than twice a minute. */
    n += clamp_u32(&s->visible_interval_s, 30, 3600);
    n += clamp_u32(&s->background_interval_s, 60, 86400);
    n += clamp_u32(&s->request_timeout_s, 3, 30);

    n += clamp_u32(&s->stale_after_s, 120, 3600);
    n += clamp_u32(&s->age_shown_after_s, 30, 3600);
    if (s->age_shown_after_s > s->stale_after_s) { s->age_shown_after_s = s->stale_after_s; n++; }
    n += clamp_u32(&s->alert_touch_ms, 0, 60000);
    n += clamp_u32(&s->alert_update_ms, 0, 60000);
    n += clamp_u8(&s->takeover_min_late_min, 1, 120);

    n += clamp_u8(&s->quiet_start_hour, 0, 23);
    n += clamp_u8(&s->quiet_end_hour, 0, 23);
    n += clamp_u32(&s->awake_after_touch_s, 10, 3600);
    s->timezone[sizeof s->timezone - 1] = '\0';
    if (!s->timezone[0]) { strcpy(s->timezone, "GMT0BST,M3.5.0/1,M10.5.0"); n++; }

    n += clamp_u16(&s->swipe_min_px, 20, 250);
    n += clamp_u16(&s->swipe_max_ms, 200, 3000);
    return n;
}
