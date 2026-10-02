#include "spike/screen_cycle.h"

#include "esp_log.h"
#include "fixtures/fixtures.h"
#include "ui/train_ui.h"

#define SCREEN_PERIOD_MS 8000

typedef enum { VIEW_BOARD, VIEW_TAKEOVER, VIEW_EMPTY } view_t;
typedef struct { int fixture; view_t view; } step_t;

/* What the device would show, in order: boards first, then the takeover each trigger fixture raises. */
static const step_t STEPS[] = {
    { FIX_LIVE, VIEW_BOARD },      { FIX_DELAYED, VIEW_BOARD },   { FIX_CANCELLED, VIEW_BOARD },
    { FIX_SCHEDULED, VIEW_BOARD }, { FIX_STALE, VIEW_BOARD },     { FIX_EMPTY, VIEW_EMPTY },
    { FIX_CANCELLED, VIEW_TAKEOVER }, { FIX_DELAYED, VIEW_TAKEOVER }, { FIX_PLATFORM, VIEW_TAKEOVER },
};
#define STEP_COUNT ((int)(sizeof STEPS / sizeof STEPS[0]))

static const char *TAG = "screen_cycle";
static lv_obj_t *s_screen;
static int s_index = -1;

static void show_next(lv_timer_t *timer)
{
    (void)timer;
    s_index = (s_index + 1) % STEP_COUNT;
    const step_t *st = &STEPS[s_index];
    const fixture_t *fx = &FIXTURES[st->fixture];
    ESP_LOGI(TAG, "screen %d/%d: %s (%s)", s_index + 1, STEP_COUNT, fx->name,
             st->view == VIEW_BOARD ? "board" : st->view == VIEW_EMPTY ? "empty" : "takeover");
    switch (st->view) {
    case VIEW_BOARD:    train_ui_board(s_screen, &fx->board, fx->age_s); break;
    case VIEW_EMPTY:    train_ui_empty(s_screen, &fx->board, fx->age_s); break;
    case VIEW_TAKEOVER: train_ui_takeover(s_screen, &fx->board, fx->age_s, board_takeover(&fx->board, fx->age_s)); break;
    }
}

void screen_cycle_start(lv_obj_t *screen)
{
    s_screen = screen;
    show_next(NULL);
    lv_timer_create(show_next, SCREEN_PERIOD_MS, NULL);
}
