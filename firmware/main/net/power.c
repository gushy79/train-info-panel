#include "net/power.h"

#include <stdlib.h>
#include <time.h>

#include "esp_timer.h"
#include "board/settings.h"

#define VALID_UNIX_TIME 1700000000   /* anything earlier means SNTP has not set the clock yet */

static volatile bool s_awake_set;
static volatile uint32_t s_awake_until_ms;
static volatile bool s_touched;
static volatile uint32_t s_last_touch_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void power_init(void)
{
    setenv("TZ", settings()->timezone, 1);
    tzset();
}

bool power_local_hour(int *hour)
{
    time_t t = time(NULL);
    if (t < VALID_UNIX_TIME) return false;
    struct tm lt;
    localtime_r(&t, &lt);
    *hour = lt.tm_hour;
    return true;
}

power_mode_t power_mode(void)
{
    const settings_t *c = settings();
    if (!c->quiet_enabled) return POWER_ACTIVE;
    quiet_cfg_t cfg = { c->quiet_start_hour, c->quiet_end_hour };
    int hour = 0;
    bool valid = power_local_hour(&hour);
    return quiet_decide(&cfg, valid, hour, now_ms(), s_awake_set, s_awake_until_ms);
}

void power_note_touch(void)
{
    s_awake_until_ms = now_ms() + settings()->awake_after_touch_s * 1000u;
    s_awake_set = true;
    s_last_touch_ms = now_ms();
    s_touched = true;
}

uint32_t power_ms_since_touch(void)
{
    return s_touched ? now_ms() - s_last_touch_ms : 0xFFFFFFFFu;
}
