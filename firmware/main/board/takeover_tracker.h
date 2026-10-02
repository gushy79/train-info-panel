#pragma once
/* When a takeover is shown: for a configured time (two settings, below), once per change. Decides *when*; board_takeover() decides *what*.
 * Pure logic, host-tested. */

#include <stdbool.h>
#include <stdint.h>

#include "board/board_logic.h"

/* How long it stays up depends on why it appeared (see settings.h): alert_touch_ms when the user has just
 * touched the panel (they are looking), alert_update_ms when a background update found it (they may not be). */

typedef struct {
    bool active;
    uint32_t shown_since_ms;
    uint32_t duration_ms;   /* chosen when the alert was announced */
    char sig[40];           /* what was last announced; empty when no takeover condition holds */
} takeover_tracker_t;

/* Returns the takeover to draw right now, or TAKEOVER_NONE for the normal board. A condition that
 * persists (same train, same state) is announced once; a changed one (new estimate, new platform)
 * is announced again; a cleared one re-arms. */
/* user_attending: the user touched the panel moments ago, so they are looking at it. A duration of 0 means
 * alerts of that kind are switched off in the config. */
takeover_t takeover_tracker_update(takeover_tracker_t *t, const board_t *b, uint32_t age_s, uint32_t now_ms, bool user_attending);
