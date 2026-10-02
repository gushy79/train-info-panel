#include "board/board_logic.h"
#include "board/settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* snprintf onto the end of out, never past n. */
static void append(char *out, size_t n, const char *fmt, ...)
{
    size_t len = strlen(out);
    if (len + 1 >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + len, n - len, fmt, ap);
    va_end(ap);
}

static uint32_t minutes(uint32_t age_s)
{
    uint32_t m = (age_s + 30) / 60;
    return m ? m : 1;
}

freshness_t board_freshness(const board_t *b, uint32_t age_s)
{
    if (b->mode == BOARD_SCHEDULED) return FRESH_TIMETABLE;
    return age_s > settings()->stale_after_s ? FRESH_STALE : FRESH_LIVE;
}

void board_chip_text(freshness_t f, uint32_t age_s, char *out, size_t n)
{
    if (f == FRESH_TIMETABLE) snprintf(out, n, "TIMETABLE");
    else if (f == FRESH_STALE) snprintf(out, n, "STALE %um", (unsigned)minutes(age_s));
    else if (age_s >= settings()->age_shown_after_s) snprintf(out, n, "LIVE %um", (unsigned)minutes(age_s));
    else snprintf(out, n, "LIVE");
}

tone_t board_row_status(freshness_t f, const board_service_t *s, char *out, size_t n)
{
    /* Stale and timetable rows never claim "On time": no false-live. */
    if (f == FRESH_TIMETABLE) { snprintf(out, n, "Timetable"); return TONE_DIM; }
    if (s->state == SVC_CANCELLED) { snprintf(out, n, "Cancelled"); return TONE_BAD; }
    if (s->state == SVC_DELAYED) {
        if (s->etd[0]) snprintf(out, n, "Exp %s", s->etd); else snprintf(out, n, "Delayed");
        return TONE_WARN;
    }
    if (f == FRESH_STALE) { snprintf(out, n, "Not live"); return TONE_DIM; }
    snprintf(out, n, "On time");
    return TONE_OK;
}

tone_t board_row_detail(freshness_t f, const board_service_t *s, char *out, size_t n)
{
    char status[24];
    tone_t tone = board_row_status(f, s, status, sizeof status);
    snprintf(out, n, "%s", status);
    if (f != FRESH_TIMETABLE) {
        if (s->plat[0]) append(out, n, " - Platform %s", s->plat);
        else if (s->state != SVC_CANCELLED) append(out, n, " - Platform -");
        if (s->plat_was[0]) append(out, n, " (was %s)", s->plat_was);
    }
    if (s->state == SVC_CANCELLED && s->why[0]) append(out, n, " - %s", s->why);
    if (s->plat_was[0] && tone == TONE_OK) tone = TONE_WARN;
    return tone;
}

tone_t board_footer(const board_t *b, freshness_t f, char *out, size_t n)
{
    out[0] = '\0';
    if (f == FRESH_TIMETABLE) { snprintf(out, n, "Timetable only: live times unavailable"); return TONE_DIM; }
    if (f == FRESH_STALE)     { snprintf(out, n, "No live update: times may be wrong"); return TONE_WARN; }
    if (b->count > 3) snprintf(out, n, "Then %s %s", b->svc[3].std, b->svc[3].dest);
    return TONE_DIM;
}

takeover_t board_takeover(const board_t *b, uint32_t age_s)
{
    if (board_freshness(b, age_s) != FRESH_LIVE || b->count == 0) return TAKEOVER_NONE;
    const board_service_t *s = &b->svc[0];
    if (s->state == SVC_CANCELLED) return TAKEOVER_CANCELLED;
    if (s->plat[0] && s->plat_was[0]) return TAKEOVER_PLATFORM;
    if (s->state == SVC_DELAYED && s->late >= settings()->takeover_min_late_min) return TAKEOVER_DELAYED;
    return TAKEOVER_NONE;
}

void board_next_line(const board_t *b, freshness_t f, char *out, size_t n)
{
    for (int i = 1; i < b->count; i++) {
        const board_service_t *s = &b->svc[i];
        if (s->state == SVC_CANCELLED) continue;
        snprintf(out, n, "Next: %s", s->std);
        if (f != FRESH_TIMETABLE) {
            if (s->plat[0]) append(out, n, " - Platform %s", s->plat);
            else append(out, n, " - platform tbc");
        }
        if (s->state == SVC_DELAYED) {
            if (s->etd[0]) append(out, n, " - exp %s", s->etd);
            else append(out, n, " - delayed");
        }
        return;
    }
    snprintf(out, n, "No later trains listed");
}
