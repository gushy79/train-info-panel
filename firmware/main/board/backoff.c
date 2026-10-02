#include "board/backoff.h"

uint32_t backoff_next_delay_s(uint32_t failures, int http_status)
{
    if (failures == 0) failures = 1;   /* callers only ask after a failure; be safe */
    if (http_status == 401 || http_status == 403) return BACKOFF_AUTH_S;
    if (http_status == 429) return BACKOFF_RATELIMIT_S;
    uint32_t d = 30;                       /* 30, 60, 120, 240, then capped */
    for (uint32_t i = 1; i < failures && d < BACKOFF_MAX_S; i++) d *= 2;
    return d > BACKOFF_MAX_S ? BACKOFF_MAX_S : d;
}
