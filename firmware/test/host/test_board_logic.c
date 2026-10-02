/* Host test for presentation rules: gcc, no ESP-IDF. Run: firmware/test/host/run.sh */
#include <stdio.h>
#include <string.h>

#include "board/board_logic.h"
#include "board/settings.h"
#include "fixtures/fixtures.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_STR(got, want) do { if (strcmp((got), (want))) { printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), (want)); failures++; } } while (0)

static takeover_t take(int i) { return board_takeover(&FIXTURES[i].board, FIXTURES[i].age_s); }
static freshness_t fresh(int i) { return board_freshness(&FIXTURES[i].board, FIXTURES[i].age_s); }

int main(void)
{
    char a[96], b[96];

    /* Mirrors the "trigger rule applied to each fixture" table in docs/design/concepts.html. */
    CHECK(take(FIX_LIVE) == TAKEOVER_NONE);
    CHECK(take(FIX_DELAYED) == TAKEOVER_DELAYED);
    CHECK(take(FIX_CANCELLED) == TAKEOVER_CANCELLED);
    CHECK(take(FIX_SCHEDULED) == TAKEOVER_NONE);
    CHECK(take(FIX_STALE) == TAKEOVER_NONE);
    CHECK(take(FIX_EMPTY) == TAKEOVER_NONE);
    CHECK(take(FIX_PLATFORM) == TAKEOVER_PLATFORM);

    CHECK(fresh(FIX_LIVE) == FRESH_LIVE);
    CHECK(fresh(FIX_SCHEDULED) == FRESH_TIMETABLE);
    CHECK(fresh(FIX_STALE) == FRESH_STALE);

    /* A takeover is a strong claim: the same cancelled board, but old, must not make one. */
    CHECK(board_takeover(&FIXTURES[FIX_CANCELLED].board, settings()->stale_after_s + 1) == TAKEOVER_NONE);
    CHECK(board_takeover(&FIXTURES[FIX_CANCELLED].board, settings()->stale_after_s) == TAKEOVER_CANCELLED);

    board_chip_text(fresh(FIX_LIVE), 20, a, sizeof a);       CHECK_STR(a, "LIVE");
    board_chip_text(FRESH_LIVE, 200, a, sizeof a);           CHECK_STR(a, "LIVE 3m");
    board_chip_text(fresh(FIX_STALE), 25 * 60, a, sizeof a); CHECK_STR(a, "STALE 25m");
    board_chip_text(FRESH_TIMETABLE, 0, a, sizeof a);        CHECK_STR(a, "TIMETABLE");

    /* Row text, no false-live: stale and timetable rows never say "On time". */
    const board_t *live = &FIXTURES[FIX_LIVE].board;
    board_row_status(FRESH_LIVE, &live->svc[0], a, sizeof a);   CHECK_STR(a, "On time");
    board_row_status(FRESH_STALE, &live->svc[0], a, sizeof a);  CHECK_STR(a, "Not live");
    board_row_status(FRESH_TIMETABLE, &live->svc[0], a, sizeof a); CHECK_STR(a, "Timetable");

    const board_t *del = &FIXTURES[FIX_DELAYED].board;
    CHECK(board_row_status(FRESH_LIVE, &del->svc[0], a, sizeof a) == TONE_WARN);
    CHECK_STR(a, "Exp 15:38");
    board_row_status(FRESH_LIVE, &del->svc[1], a, sizeof a);    CHECK_STR(a, "Delayed");

    const board_t *can = &FIXTURES[FIX_CANCELLED].board;
    CHECK(board_row_detail(FRESH_LIVE, &can->svc[0], a, sizeof a) == TONE_BAD);
    CHECK_STR(a, "Cancelled - a shortage of train crew");
    board_row_detail(FRESH_LIVE, &can->svc[1], a, sizeof a);
    CHECK_STR(a, "On time - Platform 3 (was 4)");
    /* A changed platform must stand out from a routine "On time" row. */
    CHECK(board_row_detail(FRESH_LIVE, &can->svc[1], a, sizeof a) == TONE_WARN);
    board_row_detail(FRESH_TIMETABLE, &live->svc[0], a, sizeof a); CHECK_STR(a, "Timetable");

    board_next_line(can, FRESH_LIVE, a, sizeof a);  CHECK_STR(a, "Next: 15:56 - Platform 3");
    board_next_line(del, FRESH_LIVE, b, sizeof b);  CHECK_STR(b, "Next: 15:56 - Platform 4 - delayed");

    /* Footer: warnings replace "Then ...", and are never silent on bad data. */
    CHECK(board_footer(live, FRESH_LIVE, a, sizeof a) == TONE_DIM);          CHECK_STR(a, "Then 16:59 London Euston");
    CHECK(board_footer(live, FRESH_STALE, a, sizeof a) == TONE_WARN);        CHECK_STR(a, "No live update: times may be wrong");
    CHECK(board_footer(live, FRESH_TIMETABLE, a, sizeof a) == TONE_DIM);     CHECK_STR(a, "Timetable only: live times unavailable");
    board_footer(&FIXTURES[FIX_EMPTY].board, FRESH_LIVE, a, sizeof a);       CHECK_STR(a, "");

    /* The panel font is ASCII-only here: no fixture-driven string may contain a byte >= 0x80. */
    for (int i = 0; i < FIXTURE_COUNT; i++) {
        const board_t *bd = &FIXTURES[i].board;
        for (int k = 0; k < bd->count; k++)
            for (int fr = FRESH_LIVE; fr <= FRESH_TIMETABLE; fr++) {
                board_row_detail((freshness_t)fr, &bd->svc[k], a, sizeof a);
                board_next_line(bd, (freshness_t)fr, b, sizeof b);
                for (const char *c = a; *c; c++) CHECK((unsigned char)*c < 0x80);
                for (const char *c = b; *c; c++) CHECK((unsigned char)*c < 0x80);
            }
    }

    /* Empty and over-full boards are safe. */
    board_next_line(&FIXTURES[FIX_EMPTY].board, FRESH_LIVE, a, sizeof a); CHECK_STR(a, "No later trains listed");
    CHECK(FIXTURES[FIX_EMPTY].board.count == 0);
    CHECK(FIXTURES[FIX_LIVE].board.count <= BOARD_MAX_SERVICES);

    /* Strings fit their buffers even when the detail line is long (truncation, not overflow). */
    char tiny[12];
    board_row_detail(FRESH_LIVE, &can->svc[0], tiny, sizeof tiny);
    CHECK(strlen(tiny) < sizeof tiny);

    printf(failures ? "%d FAILED\n" : "all board_logic checks passed\n", failures);
    return failures ? 1 : 0;
}
