#include "board/quiet_hours.h"

bool quiet_hour_is_quiet(const quiet_cfg_t *cfg, int hour)
{
    if (cfg->start_hour == cfg->end_hour) return false;
    if (cfg->start_hour < cfg->end_hour) return hour >= cfg->start_hour && hour < cfg->end_hour;
    return hour >= cfg->start_hour || hour < cfg->end_hour;   /* wraps midnight */
}

power_mode_t quiet_decide(const quiet_cfg_t *cfg, bool time_valid, int hour,
                          uint32_t now_ms, bool awake_set, uint32_t awake_until_ms)
{
    if (!time_valid) return POWER_ACTIVE;
    if (!quiet_hour_is_quiet(cfg, hour)) return POWER_ACTIVE;
    if (awake_set && (int32_t)(awake_until_ms - now_ms) > 0) return POWER_ACTIVE;   /* held awake by a touch */
    return POWER_QUIET;
}
