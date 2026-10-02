#include "board/schedule.h"
#include "board/settings.h"

uint32_t schedule_interval_s(int is_visible)
{
    return is_visible ? settings()->visible_interval_s : settings()->background_interval_s;
}

int schedule_pick(const int64_t *due_s, int n, int visible, int64_t now_s, int64_t last_request_s, uint32_t *wait_s)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (due_s[i] < due_s[best] || (due_s[i] == due_s[best] && i == visible)) best = i;

    int64_t wait = due_s[best] - now_s;
    int64_t gap = last_request_s + MIN_REQUEST_GAP_S - now_s;
    if (gap > wait) wait = gap;
    *wait_s = wait > 0 ? (uint32_t)wait : 0;
    return best;
}

int64_t schedule_due_after_swipe(int64_t current_due_s, int have_board, int64_t fetched_s, int64_t now_s)
{
    if (have_board && now_s - fetched_s < SWIPE_REFRESH_MIN_AGE_S) return current_due_s;   /* fresh enough */
    return current_due_s < now_s ? current_due_s : now_s;                                  /* otherwise: now */
}
