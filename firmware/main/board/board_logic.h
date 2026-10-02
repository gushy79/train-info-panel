#pragma once
/*
 * Presentation rules for the departure board, with no LVGL or ESP-IDF dependency so they can be
 * unit-tested on the host (firmware/test/host). Mirrors docs/payload.md and the rules in
 * docs/design-decision.md; the reference renders are docs/design/concepts.html.
 */

#include <stddef.h>
#include <stdint.h>

#define BOARD_MAX_SERVICES 6

typedef enum { SVC_ON_TIME, SVC_DELAYED, SVC_CANCELLED, SVC_UNKNOWN } svc_state_t;
typedef enum { BOARD_LIVE, BOARD_SCHEDULED } board_mode_t;

typedef struct {
    char std[6];        /* "HH:MM" scheduled */
    char etd[6];        /* "HH:MM" estimated, or "" */
    svc_state_t state;
    uint8_t late;       /* minutes */
    char plat[4];       /* "" when unknown */
    char plat_was[4];   /* "" unless it changed since the last poll */
    char dest[40];      /* full name; the UI truncates, never this layer */
    char why[64];
} board_service_t;

typedef struct {
    board_mode_t mode;
    char station[24];
    char label[17];     /* "Southbound", "To Euston"... "" means all trains */
    char message[84];
    uint8_t count;
    uint8_t dir_index, dir_count;   /* which direction is selected, for the header's position dots (0 = none) */
    board_service_t svc[BOARD_MAX_SERVICES];
} board_t;

/* How far to trust the data, from the device's own clock. Age is seconds since payload.generated. */
typedef enum { FRESH_LIVE, FRESH_STALE, FRESH_TIMETABLE } freshness_t;
/* Thresholds (stale_after_s, age_shown_after_s) come from settings.h. */

freshness_t board_freshness(const board_t *b, uint32_t age_s);

/* Chip text: "LIVE", "LIVE 3m", "STALE 25m", "TIMETABLE". */
void board_chip_text(freshness_t f, uint32_t age_s, char *out, size_t n);

typedef enum { TONE_OK, TONE_WARN, TONE_BAD, TONE_DIM } tone_t;

/* Short status for a row ("On time", "Exp 15:38", "Cancelled", "Timetable", "Not live"). */
tone_t board_row_status(freshness_t f, const board_service_t *s, char *out, size_t n);

/* Second line of a two-line row: status, platform, previous platform, cancellation reason.
 * ASCII only: the built-in Montserrat has no U+00B7 or ellipsis (they render as boxes on the panel).
 * A changed platform upgrades an otherwise-OK tone to a warning. */
tone_t board_row_detail(freshness_t f, const board_service_t *s, char *out, size_t n);

/* Bottom line of the board. Warns on timetable/stale data (replacing "Then ..."), else the 4th train.
 * Returns the tone to draw it in; `out` may be empty. */
tone_t board_footer(const board_t *b, freshness_t f, char *out, size_t n);

/* Takeover: only the next service, only on fresh live data. Priority cancelled > platform > delay. */
typedef enum { TAKEOVER_NONE, TAKEOVER_CANCELLED, TAKEOVER_PLATFORM, TAKEOVER_DELAYED } takeover_t;
/* The alert delay threshold (takeover_min_late_min) comes from settings.h. */

takeover_t board_takeover(const board_t *b, uint32_t age_s);

/* Footer of a takeover: the next running service after the first. */
void board_next_line(const board_t *b, freshness_t f, char *out, size_t n);
