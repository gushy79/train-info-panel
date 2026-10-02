#pragma once
#include "lvgl.h"

/* Hardware visual check: cycles every designed screen from fixture data, no networking.
 * Throwaway once the real state machine exists. */
void screen_cycle_start(lv_obj_t *screen);
