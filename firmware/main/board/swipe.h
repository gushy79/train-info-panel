#pragma once
/*
 * Horizontal swipe detection from raw touch samples. Pure logic, host-tested.
 * Coordinates are landscape screen pixels (0..319 across). A swipe fires as soon as the finger has
 * travelled far enough, mostly horizontally, quickly enough, so the screen reacts while it is still
 * moving; one touch fires at most once, and a short cooldown ignores a bounce straight after.
 */

#include <stdbool.h>
#include <stdint.h>

/* Distance (swipe_min_px) and speed (swipe_max_ms) come from settings.h. */
#define SWIPE_DOMINANCE 2      /* |dx| must be at least this many times |dy|: ignores diagonal drags */
#define SWIPE_COOLDOWN_MS 500

typedef enum { SWIPE_NONE, SWIPE_LEFT, SWIPE_RIGHT } swipe_t;

typedef struct {
    bool down;
    bool spent;            /* this touch already fired, or has run too long to count */
    int16_t x0, y0;
    uint32_t t0;
    bool fired_before;
    uint32_t last_fire_ms;
} swipe_tracker_t;

/* Feed one sample (touching=false when the finger is up). Returns the gesture, once per touch. */
swipe_t swipe_update(swipe_tracker_t *t, bool touching, int16_t x, int16_t y, uint32_t now_ms);
