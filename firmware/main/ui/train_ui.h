#pragma once
#include "board/board_logic.h"
#include "lvgl.h"

/* Each call clears `screen` and draws one view. Caller holds the LVGL lock. */

/* Concept C: header, three two-line rows, "Then ..." footer. Handles live, stale and timetable. */
void train_ui_board(lv_obj_t *screen, const board_t *b, uint32_t age_s);

/* Full-screen takeover for the next service (kind from board_takeover()). */
void train_ui_takeover(lv_obj_t *screen, const board_t *b, uint32_t age_s, takeover_t kind);

/* Live board with no services in the provider's window. */
void train_ui_empty(lv_obj_t *screen, const board_t *b, uint32_t age_s);

/* Nothing to show yet (not configured, connecting, or no first update): header, a "NO DATA" chip,
 * a headline and one explanatory line. Never styled like live data. */
void train_ui_status(lv_obj_t *screen, const char *station, const char *label, int dir_index, int dir_count,
                     const char *line1, const char *line2);
