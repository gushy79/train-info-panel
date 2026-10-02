#include "board/takeover_tracker.h"
#include "board/settings.h"

#include <stdio.h>
#include <string.h>

takeover_t takeover_tracker_update(takeover_tracker_t *t, const board_t *b, uint32_t age_s, uint32_t now_ms, bool user_attending)
{
    takeover_t kind = board_takeover(b, age_s);
    if (kind == TAKEOVER_NONE) {
        t->active = false;
        t->sig[0] = '\0';
        return TAKEOVER_NONE;
    }
    const board_service_t *s = &b->svc[0];
    char sig[sizeof t->sig];
    snprintf(sig, sizeof sig, "%d|%s|%s|%s|%s", (int)kind, s->std, s->etd, s->plat, s->plat_was);
    if (strcmp(sig, t->sig) != 0) {
        memcpy(t->sig, sig, sizeof sig);
        t->duration_ms = user_attending ? settings()->alert_touch_ms : settings()->alert_update_ms;
        t->active = t->duration_ms > 0;   /* 0 = this kind of alert is switched off */
        t->shown_since_ms = now_ms;
    }
    if (t->active && (uint32_t)(now_ms - t->shown_since_ms) < t->duration_ms) return kind;
    t->active = false;
    return TAKEOVER_NONE;
}
