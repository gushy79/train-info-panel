/* Host tests for the on-device LDBWS parser, backoff and takeover timing. See run.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board/backoff.h"
#include "board/ldbws_parse.h"
#include "board/quiet_hours.h"
#include "board/schedule.h"
#include "board/settings.h"
#include "board/swipe.h"
#include "board/takeover_tracker.h"
#include "fixtures/fixtures.h"

static int failures;
static swipe_t drag(swipe_tracker_t *t, int x0, int y0, int x1, int y1, uint32_t start_ms, uint32_t ms);
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#ifndef TESTDATA_DIR
#error "TESTDATA_DIR must point at the repo's testdata/ldbws directory"
#endif

static char *slurp(const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", TESTDATA_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(*len + 1);
    if (fread(buf, 1, *len, f) != *len) exit(2);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static int same_service(const board_service_t *a, const board_service_t *b)
{
    return !strcmp(a->std, b->std) && !strcmp(a->etd, b->etd) && a->state == b->state && a->late == b->late &&
           !strcmp(a->plat, b->plat) && !strcmp(a->plat_was, b->plat_was) && !strcmp(a->dest, b->dest) &&
           !strcmp(a->why, b->why);
}

/* The C parser and the Python adapter (which generated FIXTURES) must agree, field for field. */
static void expect_matches_fixture(const char *file, int fixture, const prev_platforms_t *prev_in)
{
    size_t len;
    char *json = slurp(file, &len);
    board_t b;
    int64_t gen = 0;
    prev_platforms_t prev = prev_in ? *prev_in : (prev_platforms_t){0};
    CHECK(ldbws_parse(json, len, "Southbound", &b, &gen, &prev) == LDB_OK);
    const board_t *want = &FIXTURES[fixture].board;
    CHECK(b.mode == want->mode);
    CHECK(!strcmp(b.station, want->station));
    CHECK(!strcmp(b.label, want->label));
    if (strcmp(b.message, want->message)) printf("  message got \"%s\" want \"%s\"\n", b.message, want->message);
    CHECK(!strcmp(b.message, want->message));
    CHECK(b.count == want->count);
    for (int i = 0; i < b.count && i < want->count; i++) {
        if (!same_service(&b.svc[i], &want->svc[i]))
            printf("  %s svc %d differs: got %s/%s/%d/%u/%s/%s/%s/%s want %s/%s/%d/%u/%s/%s/%s/%s\n", file, i,
                   b.svc[i].std, b.svc[i].etd, b.svc[i].state, b.svc[i].late, b.svc[i].plat, b.svc[i].plat_was, b.svc[i].dest, b.svc[i].why,
                   want->svc[i].std, want->svc[i].etd, want->svc[i].state, want->svc[i].late, want->svc[i].plat, want->svc[i].plat_was, want->svc[i].dest, want->svc[i].why);
        CHECK(same_service(&b.svc[i], &want->svc[i]));
    }
    CHECK(gen == 1790950667);   /* 2026-10-02T15:17:47+01:00, 7 fractional digits and all */
    free(json);
}

static void test_parser_matches_adapter(void)
{
    expect_matches_fixture("southbound_normal.json", FIX_LIVE, NULL);
    expect_matches_fixture("southbound_delayed.json", FIX_DELAYED, NULL);
    prev_platforms_t prev = { .n = 1 };
    strcpy(prev.e[0].id, "MOCK0002");
    strcpy(prev.e[0].plat, "4");
    expect_matches_fixture("southbound_cancelled.json", FIX_CANCELLED, &prev);
    expect_matches_fixture("southbound_empty.json", FIX_EMPTY, NULL);
}

static void test_unavailable_is_not_an_empty_board(void)
{
    size_t len;
    char *json = slurp("southbound_unavailable.json", &len);
    board_t b = { .count = 99 };
    CHECK(ldbws_parse(json, len, "", &b, NULL, NULL) == LDB_UNAVAILABLE);
    CHECK(b.count == 99);   /* untouched */
    free(json);
}

static void test_platform_memory_across_polls(void)
{
    size_t len;
    char *json = slurp("southbound_normal.json", &len);
    board_t b;
    prev_platforms_t prev = {0};
    CHECK(ldbws_parse(json, len, "", &b, NULL, &prev) == LDB_OK);
    CHECK(prev.n == 4 && !strcmp(prev.e[0].plat, "4"));
    CHECK(b.svc[0].plat_was[0] == '\0');                  /* first poll cannot know */
    prev.e[1].plat[0] = '3';                              /* pretend service 2 was on platform 3 */
    CHECK(ldbws_parse(json, len, "", &b, NULL, &prev) == LDB_OK);
    CHECK(!strcmp(b.svc[1].plat_was, "3") && !strcmp(b.svc[1].plat, "4"));
    CHECK(b.svc[0].plat_was[0] == '\0');
    free(json);
}

/* Untrusted input: every truncation of a real board, and a set of hostile shapes, must be handled
 * without a crash or leak (ASan/UBSan run this) and must never claim success with garbage. */
static void test_hostile_input(void)
{
    size_t len;
    char *json = slurp("southbound_cancelled.json", &len);
    for (size_t cut = 0; cut < len; cut++) {
        board_t b;
        ldb_result_t r = ldbws_parse(json, cut, "", &b, NULL, NULL);
        CHECK(r != LDB_OK || b.count <= BOARD_MAX_SERVICES);
    }
    free(json);

    static const char *BAD[] = {
        "", "null", "[]", "42", "\"x\"", "{}", "{\"locationName\":5}",
        "{\"locationName\":\"X\",\"trainServices\":\"nope\"}",
        "{\"locationName\":\"X\",\"trainServices\":[1,null,\"a\",[],{}]}",
        "{\"locationName\":\"X\",\"trainServices\":[{\"std\":\"99:99\"},{\"std\":5},{\"std\":\"12:00\",\"destination\":7}]}",
        "{\"locationName\":\"X\",\"nrccMessages\":[1,{\"Value\":5},{},null]}",
        "{\"locationName\":\"X\",\"generatedAt\":\"not a date\"}",
        "{\"locationName\":\"X\",\"generatedAt\":\"2026-10-02T15:17:47\"}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        board_t b = { .count = 77 };
        int64_t gen = -1;
        ldb_result_t r = ldbws_parse(BAD[i], strlen(BAD[i]), "", &b, &gen, NULL);
        if (r != LDB_OK) CHECK(b.count == 77);
        else CHECK(b.count <= BOARD_MAX_SERVICES);
    }

    /* A "good" board whose strings are enormous, non-ASCII, and full of markup. */
    static char big[8192];
    char *p = big;
    p += sprintf(p, "{\"locationName\":\"Caf\xC3\xA9 %.*s\",\"nrccMessages\":[\"<p>", 200, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    for (int i = 0; i < 300; i++) p += sprintf(p, "word &amp; <b>x</b> ");
    p += sprintf(p, "\"],\"trainServices\":[{\"std\":\"12:00\",\"etd\":\"Delayed\",\"delayReason\":\"%.*s\"}]}", 400,
                 "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr");
    board_t b;
    CHECK(ldbws_parse(big, strlen(big), "", &b, NULL, NULL) == LDB_OK);
    CHECK(strlen(b.message) <= 80);
    CHECK(strstr(b.message, "<") == NULL && strstr(b.message, "&amp;") == NULL);
    CHECK(strlen(b.svc[0].why) <= 60);
    for (const char *c = b.station; *c; c++) CHECK((unsigned char)*c < 0x80);
}

static void test_backoff(void)
{
    CHECK(backoff_next_delay_s(0, 200) == 30);   /* 0 failures is a caller bug; treated as the first failure */
    CHECK(backoff_next_delay_s(1, 0) == 30);
    CHECK(backoff_next_delay_s(2, 500) == 60);
    CHECK(backoff_next_delay_s(3, 0) == 120);
    CHECK(backoff_next_delay_s(4, 0) == 240);
    CHECK(backoff_next_delay_s(5, 0) == BACKOFF_MAX_S);
    CHECK(backoff_next_delay_s(1000000, 0) == BACKOFF_MAX_S);
    CHECK(backoff_next_delay_s(1, 403) == BACKOFF_AUTH_S && backoff_next_delay_s(1, 401) == BACKOFF_AUTH_S);
    CHECK(backoff_next_delay_s(1, 429) == BACKOFF_RATELIMIT_S);
    for (uint32_t f = 1; f < 40; f++) CHECK(backoff_next_delay_s(f, 0) >= 30 && backoff_next_delay_s(f, 0) <= BACKOFF_MAX_S);
}

static void test_schedule(void)
{
    uint32_t w;
    int64_t due[3] = { 100, 40, 40 };
    CHECK(schedule_pick(due, 3, 2, 10, -100, &w) == 2 && w == 30);        /* tie: visible wins; waits until due */
    CHECK(schedule_pick(due, 3, 0, 10, -100, &w) == 1 && w == 30);        /* tie, visible not involved: lowest index */
    CHECK(schedule_pick(due, 3, 1, 50, -100, &w) == 1 && w == 0);         /* due now */
    CHECK(schedule_pick(due, 3, 1, 50, 48, &w) == 1 && w == MIN_REQUEST_GAP_S - 2);   /* gap floor beats "due" */
    CHECK(schedule_pick(due, 3, 1, 5000, 4999, &w) == 1 && w == MIN_REQUEST_GAP_S - 1);

    CHECK(schedule_interval_s(1) == settings()->visible_interval_s && schedule_interval_s(0) == settings()->background_interval_s);
    /* request budget: 1 visible + 2 background per minute-equivalent, per month, well under 100k */
    double per_month = (1.0 / settings()->visible_interval_s + 2.0 / settings()->background_interval_s) * 60 * 60 * 24 * 30;
    CHECK(per_month < 75000);

    /* after a swipe: stale or missing board refreshes now; a fresh one is left alone */
    CHECK(schedule_due_after_swipe(9999, 0, 0, 500) == 500);              /* never fetched: now */
    CHECK(schedule_due_after_swipe(9999, 1, 100, 500) == 500);            /* 400 s old: now */
    CHECK(schedule_due_after_swipe(9999, 1, 495, 500) == 9999);           /* 5 s old: leave */
    CHECK(schedule_due_after_swipe(300, 1, 100, 500) == 300);             /* already overdue: stays earlier */
}

static void test_quiet_hours(void)
{
    quiet_cfg_t night = { .start_hour = 21, .end_hour = 6 };   /* 9 pm to 6 am, wraps midnight */
    for (int h = 0; h < 24; h++) {
        bool want = h >= 21 || h < 6;
        CHECK(quiet_hour_is_quiet(&night, h) == want);
    }
    CHECK(!quiet_hour_is_quiet(&night, 20) && quiet_hour_is_quiet(&night, 21));   /* 21:00 starts it */
    CHECK(quiet_hour_is_quiet(&night, 5) && !quiet_hour_is_quiet(&night, 6));     /* 06:00 ends it */

    quiet_cfg_t day = { .start_hour = 1, .end_hour = 5 };       /* a window that does not wrap */
    CHECK(!quiet_hour_is_quiet(&day, 0) && quiet_hour_is_quiet(&day, 1) && quiet_hour_is_quiet(&day, 4) && !quiet_hour_is_quiet(&day, 5));
    quiet_cfg_t never = { .start_hour = 6, .end_hour = 6 };
    for (int h = 0; h < 24; h++) CHECK(!quiet_hour_is_quiet(&never, h));

    /* decisions */
    CHECK(quiet_decide(&night, true, 12, 1000, false, 0) == POWER_ACTIVE);          /* daytime */
    CHECK(quiet_decide(&night, true, 23, 1000, false, 0) == POWER_QUIET);           /* night, untouched */
    CHECK(quiet_decide(&night, false, 23, 1000, false, 0) == POWER_ACTIVE);         /* clock not set: never dark */
    CHECK(quiet_decide(&night, true, 23, 1000, true, 1000 + 1) == POWER_ACTIVE);    /* held awake by a touch */
    CHECK(quiet_decide(&night, true, 23, 1000, true, 1000) == POWER_QUIET);         /* window just ended */
    CHECK(quiet_decide(&night, true, 23, 5000, true, 1000) == POWER_QUIET);         /* long ago */
    CHECK(quiet_decide(&night, true, 23, 0xFFFFFFF0u, true, 0x00000050u) == POWER_ACTIVE);   /* ms counter wrapped inside the window */
    CHECK(quiet_decide(&night, true, 23, 0x00000100u, true, 0xFFFFFFF0u) == POWER_QUIET);    /* ... and after it */
}

static void test_settings(void)
{
    settings_t s;
    settings_set_defaults(&s);
    CHECK(settings_sanitise(&s) == 0);                          /* the defaults are themselves valid */
    CHECK(s.direction_count == 3 && !strcmp(s.directions[0].label, "Southbound") && !strcmp(s.directions[0].toward, "HML"));

    /* a config with everything out of range is clamped to safe values, never trusted */
    settings_t bad;
    settings_set_defaults(&bad);
    bad.visible_interval_s = 1;  bad.background_interval_s = 5;  bad.request_timeout_s = 0;
    bad.stale_after_s = 5;       bad.alert_touch_ms = 999999;    bad.alert_update_ms = 999999;   bad.takeover_min_late_min = 0;
    bad.quiet_start_hour = 40;   bad.swipe_min_px = 1;           bad.num_rows = 99;
    bad.direction_count = 0;     bad.awake_after_touch_s = 0;    bad.timezone[0] = '\0';
    CHECK(settings_sanitise(&bad) >= 10);
    CHECK(bad.visible_interval_s == 30 && bad.background_interval_s == 60 && bad.request_timeout_s == 3);
    CHECK(bad.stale_after_s == 120 && bad.alert_touch_ms == 60000 && bad.alert_update_ms == 60000 && bad.takeover_min_late_min == 1);
    CHECK(bad.quiet_start_hour == 23 && bad.swipe_min_px == 20 && bad.num_rows == 10);
    CHECK(bad.direction_count == 1 && bad.awake_after_touch_s == 10 && bad.timezone[0] != '\0');
    CHECK(bad.age_shown_after_s <= bad.stale_after_s);

    /* text: CRS codes must be three capitals; labels are plain ASCII and never empty */
    settings_t t;
    settings_set_defaults(&t);
    strcpy(t.directions[0].toward, "rdg");                      /* lower case is not a CRS */
    strcpy(t.directions[1].label, "");                          /* empty label */
    strcpy(t.directions[2].label, "Caf\xC3\xA9");               /* non-ASCII */
    strcpy(t.station_crs, "BKMX");
    CHECK(settings_sanitise(&t) >= 4);
    CHECK(t.directions[0].toward[0] == '\0');                   /* fell back to "all trains", the safe choice */
    CHECK(t.directions[1].label[0] != '\0');
    for (const char *c = t.directions[2].label; *c; c++) CHECK((unsigned char)*c < 0x80);
    CHECK(strlen(t.station_crs) == 3);

    /* behaviour follows the live settings (these are the user-facing knobs) */
    const board_t *delayed = &FIXTURES[FIX_DELAYED].board;      /* first train 12 min late */
    const board_t *cancelled = &FIXTURES[FIX_CANCELLED].board;
    settings_t live;
    settings_set_defaults(&live);
    live.takeover_min_late_min = 20;
    settings_install(&live);
    CHECK(board_takeover(delayed, 30) == TAKEOVER_NONE);        /* 12 min is below a 20 min threshold */
    live.takeover_min_late_min = 5;
    settings_install(&live);
    CHECK(board_takeover(delayed, 30) == TAKEOVER_DELAYED);
    settings_set_defaults(&live);
    live.stale_after_s = 300;
    settings_install(&live);
    CHECK(board_freshness(&FIXTURES[FIX_LIVE].board, 301) == FRESH_STALE);
    CHECK(board_freshness(&FIXTURES[FIX_LIVE].board, 300) == FRESH_LIVE);
    live.alert_touch_ms = 2000;
    live.alert_update_ms = 9000;
    settings_install(&live);
    takeover_tracker_t t2 = {0};
    CHECK(takeover_tracker_update(&t2, cancelled, 30, 100, true) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&t2, cancelled, 30, 2099, true) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&t2, cancelled, 30, 2100, true) == TAKEOVER_NONE);   /* the configured 2 s */
    takeover_tracker_t t4 = {0};
    CHECK(takeover_tracker_update(&t4, cancelled, 30, 100, false) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&t4, cancelled, 30, 9099, false) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&t4, cancelled, 30, 9100, false) == TAKEOVER_NONE);  /* the configured 9 s */
    live.swipe_min_px = 200;
    settings_install(&live);
    swipe_tracker_t sw = {0};
    CHECK(drag(&sw, 250, 80, 150, 80, 1000, 300) == SWIPE_NONE);   /* 100 px: enough by default, not for 200 */
    settings_set_defaults(&live);
    settings_install(&live);                                    /* leave the defaults for the other tests */
}

static void test_takeover_timing(void)
{
    takeover_tracker_t t = {0};
    const board_t *cancelled = &FIXTURES[FIX_CANCELLED].board;
    uint32_t age = FIXTURES[FIX_CANCELLED].age_s;

    CHECK(takeover_tracker_update(&t, cancelled, age, 1000, true) == TAKEOVER_CANCELLED);          /* announced */
    CHECK(takeover_tracker_update(&t, cancelled, age, 1000 + settings()->alert_touch_ms - 1, true) == TAKEOVER_CANCELLED);  /* still within the window */
    CHECK(takeover_tracker_update(&t, cancelled, age, 1000 + settings()->alert_touch_ms, true) == TAKEOVER_NONE);       /* then the board */
    CHECK(takeover_tracker_update(&t, cancelled, age, 1000 + 60000, true) == TAKEOVER_NONE);       /* not re-announced */

    board_t changed = FIXTURES[FIX_DELAYED].board;                                            /* 12 min late */
    CHECK(takeover_tracker_update(&t, &changed, 30, 100000, true) == TAKEOVER_DELAYED);            /* new condition */
    CHECK(takeover_tracker_update(&t, &changed, 30, 120000, true) == TAKEOVER_NONE);
    strcpy(changed.svc[0].etd, "15:41");                                                     /* worse: new estimate */
    CHECK(takeover_tracker_update(&t, &changed, 30, 121000, true) == TAKEOVER_DELAYED);            /* announced again */

    CHECK(takeover_tracker_update(&t, &FIXTURES[FIX_LIVE].board, 20, 200000, true) == TAKEOVER_NONE);  /* cleared: re-arms */
    CHECK(takeover_tracker_update(&t, cancelled, age, 300000, true) == TAKEOVER_CANCELLED);            /* returns: announced */

    /* Stale data never announces, however bad it looks. */
    takeover_tracker_t t2 = {0};
    CHECK(takeover_tracker_update(&t2, cancelled, settings()->stale_after_s + 1, 5, true) == TAKEOVER_NONE);

    /* Two situations, two lengths: just touched (short) vs found by a background update (long). */
    takeover_tracker_t tu = {0}, tt = {0};
    CHECK(takeover_tracker_update(&tu, cancelled, age, 1000, false) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&tu, cancelled, age, 1000 + settings()->alert_touch_ms + 1, false) == TAKEOVER_CANCELLED);   /* still up past the short time */
    CHECK(takeover_tracker_update(&tu, cancelled, age, 1000 + settings()->alert_update_ms - 1, false) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&tu, cancelled, age, 1000 + settings()->alert_update_ms, false) == TAKEOVER_NONE);
    CHECK(takeover_tracker_update(&tt, cancelled, age, 1000, true) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&tt, cancelled, age, 1000 + settings()->alert_touch_ms, true) == TAKEOVER_NONE);             /* gone at the short time */
    CHECK(settings()->alert_update_ms > settings()->alert_touch_ms);                                                          /* defaults: longer when unattended */
    {   /* the length is fixed when the alert appears: touching the panel mid-alert does not cut an unattended one short */
        takeover_tracker_t tm = {0};
        CHECK(takeover_tracker_update(&tm, cancelled, age, 0, false) == TAKEOVER_CANCELLED);
        CHECK(takeover_tracker_update(&tm, cancelled, age, settings()->alert_touch_ms + 100, true) == TAKEOVER_CANCELLED);
    }
    {   /* each kind can be switched off on its own with 0 */
        settings_t off; settings_set_defaults(&off);
        off.alert_update_ms = 0;
        settings_install(&off);
        takeover_tracker_t tz = {0};
        CHECK(takeover_tracker_update(&tz, cancelled, age, 10, false) == TAKEOVER_NONE);   /* background alerts off */
        CHECK(takeover_tracker_update(&tz, cancelled, age, 20, true) == TAKEOVER_NONE);    /* same condition: already considered announced */
        takeover_tracker_t tz2 = {0};
        CHECK(takeover_tracker_update(&tz2, cancelled, age, 10, true) == TAKEOVER_CANCELLED);  /* touch alerts still on */
        settings_set_defaults(&off);
        settings_install(&off);
    }

    /* millisecond counter wrap-around does not re-trigger or stick */
    takeover_tracker_t t3 = {0};
    CHECK(takeover_tracker_update(&t3, cancelled, age, 0xFFFFFF00u, true) == TAKEOVER_CANCELLED);
    CHECK(takeover_tracker_update(&t3, cancelled, age, 0xFFFFFF00u + settings()->alert_touch_ms + 5000u, true) == TAKEOVER_NONE);
}

/* Feed a straight-line drag: from (x0,y0) to (x1,y1) over ms, sampled every 20 ms; returns the first gesture. */
static swipe_t drag(swipe_tracker_t *t, int x0, int y0, int x1, int y1, uint32_t start_ms, uint32_t ms)
{
    swipe_t result = SWIPE_NONE;
    for (uint32_t d = 0; d <= ms; d += 20) {
        int x = x0 + (int)((long)(x1 - x0) * (long)d / (long)ms), y = y0 + (int)((long)(y1 - y0) * (long)d / (long)ms);
        swipe_t g = swipe_update(t, true, (int16_t)x, (int16_t)y, start_ms + d);
        if (g != SWIPE_NONE && result == SWIPE_NONE) result = g;
    }
    swipe_update(t, false, 0, 0, start_ms + ms + 20);   /* lift */
    return result;
}

static void test_swipe(void)
{
    swipe_tracker_t t = {0};
    CHECK(drag(&t, 250, 80, 60, 90, 1000, 300) == SWIPE_LEFT);     /* flick left */
    CHECK(drag(&t, 60, 90, 250, 80, 3000, 300) == SWIPE_RIGHT);    /* flick right */

    swipe_tracker_t a = {0};
    CHECK(drag(&a, 150, 80, 160, 80, 1000, 300) == SWIPE_NONE);    /* a tap or jitter */
    CHECK(drag(&a, 150, 40, 215, 40, 3000, 300) == SWIPE_NONE);    /* too short (65 px) */
    CHECK(drag(&a, 100, 20, 200, 150, 5000, 300) == SWIPE_NONE);   /* diagonal: dx 100, dy 130 */
    CHECK(drag(&a, 250, 80, 60, 80, 7000, 4000) == SWIPE_NONE);    /* too slow: reaches 70 px only after ~1.5 s */
    CHECK(drag(&a, 250, 80, 60, 80, 12000, 300) == SWIPE_LEFT);   /* ...and the next real one still works */

    /* fires once per touch, while the finger is still down */
    swipe_tracker_t b = {0};
    int fired = 0;
    for (int i = 0; i < 40; i++) fired += swipe_update(&b, true, (int16_t)(280 - i * 8), 80, 1000 + (uint32_t)i * 20) != SWIPE_NONE;
    CHECK(fired == 1);
    swipe_update(&b, false, 0, 0, 1900);

    /* a bounce straight after a swipe is ignored; a later one is not */
    swipe_tracker_t c = {0};
    CHECK(drag(&c, 250, 80, 60, 80, 1000, 200) == SWIPE_LEFT);
    CHECK(drag(&c, 250, 80, 60, 80, 1300, 200) == SWIPE_NONE);     /* 300 ms later: inside cooldown */
    CHECK(drag(&c, 250, 80, 60, 80, 2500, 200) == SWIPE_LEFT);

    /* wrap-around of the millisecond counter */
    swipe_tracker_t w = {0};
    CHECK(drag(&w, 250, 80, 60, 80, 0xFFFFFF80u, 300) == SWIPE_LEFT);
    CHECK(drag(&w, 250, 80, 60, 80, 0xFFFFFF80u + 2000u, 300) == SWIPE_LEFT);

    /* no touch, no gesture, no matter what */
    swipe_tracker_t n = {0};
    for (int i = 0; i < 100; i++) CHECK(swipe_update(&n, false, (int16_t)(i * 3), 5, (uint32_t)i * 10) == SWIPE_NONE);
}

int main(void)
{
    test_parser_matches_adapter();
    test_unavailable_is_not_an_empty_board();
    test_platform_memory_across_polls();
    test_hostile_input();
    test_backoff();
    test_swipe();
    test_takeover_timing();
    test_schedule();
    test_quiet_hours();
    test_settings();
    printf(failures ? "%d FAILED\n" : "all net_logic checks passed\n", failures);
    return failures ? 1 : 0;
}
