#include "app/app_view.h"

#include <stdio.h>
#include <string.h>

#include "board/takeover_tracker.h"
#include "display/bsp_display.h"
#include "net/power.h"
#include "net/poller.h"
#include "ui/train_ui.h"

typedef struct {
    int view;              /* 0 none, 1 status, 2 board, 3 empty, 4 takeover */
    int dir;
    uint32_t generation;
    int takeover;
    char chip[16];
    char text[96];
} drawn_t;

#define USER_ATTENTION_MS 10000

static drawn_t s_drawn;
static takeover_tracker_t s_tracker;
static bool s_screen_on = true;     /* backlight and panel */
static bool s_pending_on;           /* waking: draw first, light the screen on the next tick (no flash of old content) */

void app_view_tick(lv_obj_t *screen, uint32_t now_ms)
{
    /* Quiet hours: the screen is off and nothing is drawn. Entering them forgets the cached boards
     * (by morning they are hours old); leaving them redraws before the backlight comes on. */
    if (power_mode() == POWER_QUIET) {
        if (s_screen_on) {
            bsp_display_set_power(false);
            s_screen_on = false;
            s_pending_on = false;
            poller_clear_cache();
            memset(&s_drawn, 0, sizeof s_drawn);
            memset(&s_tracker, 0, sizeof s_tracker);
        }
        return;
    }
    if (!s_screen_on) {
        if (s_pending_on) {
            bsp_display_set_power(true);
            s_screen_on = true;
            s_pending_on = false;
        } else {
            s_pending_on = true;   /* fall through: draw the current state this tick, show it on the next */
        }
    }

    static net_snapshot_t s;   /* ~1.2 KB: keep it off the LVGL task's stack */
    poller_snapshot(&s);
    drawn_t want = { 0 };
    want.dir = s.dir_index;
    s.board.dir_index = s.dir_index;   /* for the header's position dots */
    s.board.dir_count = s.dir_count;

    if (!s.have_board) {
        const char *l1, *l2 = s.reason;
        if (s.state == NET_NO_CREDS)      { l1 = "Not configured"; l2 = "Run tools/provision.py over USB"; }
        else if (s.reason[0])             { l1 = "No live data"; }
        else                              { l1 = "Loading..."; l2 = s.label; }
        want.view = 1;
        snprintf(want.text, sizeof want.text, "%s|%s", l1, l2);
        if (memcmp(&want, &s_drawn, sizeof want) != 0) {
            train_ui_status(screen, s.station_name, s.label, s.dir_index, s.dir_count, l1, l2);
            s_drawn = want;
        }
        return;
    }

    freshness_t f = board_freshness(&s.board, s.age_s);
    /* Touched in the last 10 s (a swipe, or the tap that woke the screen): they are looking, so a short alert.
     * Otherwise a background update found it and they may not be: a longer one. */
    bool attending = power_ms_since_touch() < USER_ATTENTION_MS;
    takeover_t tk = takeover_tracker_update(&s_tracker, &s.board, s.age_s, now_ms, attending);
    want.view = tk != TAKEOVER_NONE ? 4 : (s.board.count == 0 ? 3 : 2);
    want.generation = s.generation;
    want.takeover = tk;
    board_chip_text(f, s.age_s, want.chip, sizeof want.chip);   /* changes once a minute, so the age stays honest */

    if (memcmp(&want, &s_drawn, sizeof want) == 0) return;
    switch (want.view) {
    case 4: train_ui_takeover(screen, &s.board, s.age_s, tk); break;
    case 3: train_ui_empty(screen, &s.board, s.age_s); break;
    default: train_ui_board(screen, &s.board, s.age_s); break;
    }
    s_drawn = want;
}
