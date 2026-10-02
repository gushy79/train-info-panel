#pragma once
#include "lvgl.h"

/* Call about once a second from the LVGL task: reads the poller's snapshot, decides what to show
 * (status / board / empty / takeover) and redraws only when something changed. */
void app_view_tick(lv_obj_t *screen, uint32_t now_ms);
