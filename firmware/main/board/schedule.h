#pragma once
/* Which direction to fetch next, and when. Pure logic, host-tested.
 *
 * Every direction keeps its own cached board so a swipe shows data at once. The one on screen is
 * refreshed often; the others only occasionally, to keep total requests modest (about 1.5 a minute for
 * three directions at the defaults, against a free tier believed to be ~100k a month; tools/provision.py prints an
 * estimate for your config; see docs/data-source.md). */

#include <stdint.h>

/* The two refresh intervals (visible_interval_s, background_interval_s) come from settings.h. */
#define SWIPE_REFRESH_MIN_AGE_S 10  /* swiping back and forth does not re-fetch what is seconds old */
#define MIN_REQUEST_GAP_S 5         /* hard floor between any two requests */

uint32_t schedule_interval_s(int is_visible);

/* Pick the slot with the earliest due time (ties go to the visible one). Returns its index and sets
 * *wait_s to how long until it may be fetched: 0 means now. last_request_s enforces the gap. */
int schedule_pick(const int64_t *due_s, int n, int visible, int64_t now_s, int64_t last_request_s, uint32_t *wait_s);

/* After a swipe onto `slot`: when should it next be fetched? Immediately, unless its board is fresh. */
int64_t schedule_due_after_swipe(int64_t current_due_s, int have_board, int64_t fetched_s, int64_t now_s);
