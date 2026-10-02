#pragma once
/*
 * Every user-adjustable behaviour value, in one struct. The values come from the device's NVS, which
 * tools/provision.py fills from the user's config.ini, so changing them needs no code change and no
 * rebuild. Compiled-in defaults are used for anything missing, and every value is clamped to a safe
 * range, so a typo in config.ini can never wedge the panel or hammer the API.
 *
 * No ESP-IDF dependency: host-tested. Values that are safety floors or hardware facts (the 5 s minimum
 * gap between requests, the backoff schedule, I2C timeouts) are deliberately NOT here.
 */

#include <stdbool.h>
#include <stdint.h>

#define MAX_DIRECTIONS 5

typedef struct {
    char label[17];    /* shown in the header: "Southbound" */
    char toward[4];    /* CRS of a station the trains must call at; "" = all trains */
} direction_cfg_t;

typedef struct {
    /* what to watch */
    char station_crs[4];
    char station_name[24];
    uint8_t direction_count;                       /* 1..MAX_DIRECTIONS; the first is the default */
    direction_cfg_t directions[MAX_DIRECTIONS];
    uint8_t num_rows;                              /* LDBWS rows requested per board */

    /* polling */
    uint32_t visible_interval_s;                   /* the direction on screen */
    uint32_t background_interval_s;                /* the others, kept warm for instant swipes */
    uint32_t request_timeout_s;

    /* warnings */
    uint32_t stale_after_s;                        /* no update for this long: STALE */
    uint32_t age_shown_after_s;                    /* "LIVE 3m": show the age from this point */
    uint32_t alert_touch_ms;                       /* full-screen alert length when you just touched the panel; 0 = off */
    uint32_t alert_update_ms;                      /* ... when a background update found it and you may not be looking; 0 = off */
    uint8_t takeover_min_late_min;                 /* a delay of at least this many minutes is an alert */

    /* quiet hours */
    bool quiet_enabled;
    uint8_t quiet_start_hour, quiet_end_hour;      /* local time; start > end wraps midnight */
    uint32_t awake_after_touch_s;                  /* a touch keeps the screen on this long */
    char timezone[48];                             /* POSIX TZ string */

    /* touch */
    uint16_t swipe_min_px;                         /* how far a swipe must travel (of 320) */
    uint16_t swipe_max_ms;                         /* ... and how quickly */
} settings_t;

/* Live settings; never NULL. Defaults until settings_install() is called. */
const settings_t *settings(void);

void settings_set_defaults(settings_t *s);

/* Clamp every value into its safe range, fix up directions, terminate strings. Returns how many values
 * had to be changed (0 = already valid). */
int settings_sanitise(settings_t *s);

/* Make `s` the live settings (call once at boot, before other tasks run; tests call it freely). */
void settings_install(const settings_t *s);
