#pragma once
/*
 * Quiet hours: overnight nobody is catching a train, so the panel stops polling and turns the screen
 * off. A touch wakes it for a while. Pure logic (the clock and the display are the caller's job),
 * host-tested.
 *
 * Fail-safe: until the clock has been set from the network the panel behaves as if it is in
 * active hours, so a fresh boot or a dead NTP never leaves it dark or silent.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum { POWER_ACTIVE, POWER_QUIET } power_mode_t;

typedef struct {
    int start_hour;   /* quiet from start_hour:00 ...                                   */
    int end_hour;     /* ... until end_hour:00 (exclusive). start > end wraps midnight. */
                      /* start == end means there are no quiet hours.                   */
} quiet_cfg_t;

/* Is this local hour (0-23) inside the quiet window? */
bool quiet_hour_is_quiet(const quiet_cfg_t *cfg, int hour);

/* Decide the power mode.
 *   time_valid  - false until the clock is set; forces POWER_ACTIVE
 *   hour        - local hour of day
 *   awake_set / awake_until_ms - a touch holds the panel awake until this millisecond tick
 * Millisecond comparison is wrap-safe. */
power_mode_t quiet_decide(const quiet_cfg_t *cfg, bool time_valid, int hour,
                          uint32_t now_ms, bool awake_set, uint32_t awake_until_ms);
