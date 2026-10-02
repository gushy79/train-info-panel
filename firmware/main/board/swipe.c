#include "board/swipe.h"
#include "board/settings.h"

static int abs16(int v) { return v < 0 ? -v : v; }

swipe_t swipe_update(swipe_tracker_t *t, bool touching, int16_t x, int16_t y, uint32_t now_ms)
{
    if (!touching) {
        t->down = false;
        return SWIPE_NONE;
    }
    if (!t->down) {                       /* finger just landed */
        t->down = true;
        t->spent = t->fired_before && (uint32_t)(now_ms - t->last_fire_ms) < SWIPE_COOLDOWN_MS;
        t->x0 = x;
        t->y0 = y;
        t->t0 = now_ms;
        return SWIPE_NONE;
    }
    if (t->spent) return SWIPE_NONE;
    if ((uint32_t)(now_ms - t->t0) > settings()->swipe_max_ms) { t->spent = true; return SWIPE_NONE; }

    int dx = x - t->x0, dy = y - t->y0;
    if (abs16(dx) < (int)settings()->swipe_min_px || abs16(dx) < SWIPE_DOMINANCE * abs16(dy)) return SWIPE_NONE;

    t->spent = true;
    t->fired_before = true;
    t->last_fire_ms = now_ms;
    return dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
}
