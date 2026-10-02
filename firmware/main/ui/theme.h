#pragma once
/* Palette from docs/design-decision.md. Colour is never the only cue: each tone also changes text. */
#include "lvgl.h"

#define COL_BG          lv_color_hex(0x000000)
#define COL_TEXT        lv_color_hex(0xFFFFFF)
#define COL_OK          lv_color_hex(0x4ADE80)
#define COL_WARN        lv_color_hex(0xFBBF24)
#define COL_BAD         lv_color_hex(0xFF5A5A)
#define COL_DIM         lv_color_hex(0x9A9A9A)
#define COL_HDR_BG      lv_color_hex(0x1A1A1A)
#define COL_HDR_STALE   lv_color_hex(0x2A2000)
#define COL_RULE        lv_color_hex(0x262626)
#define COL_CHIP_LIVE_BG  lv_color_hex(0x14532D)
#define COL_CHIP_LIVE_FG  lv_color_hex(0xBBF7D0)
#define COL_CHIP_SCHED_BG lv_color_hex(0x333333)
#define COL_CHIP_SCHED_FG lv_color_hex(0xDDDDDD)
#define COL_CHIP_SCHED_BD lv_color_hex(0x888888)
#define COL_BAND_RED    lv_color_hex(0xC62828)
#define COL_BAND_AMBER  lv_color_hex(0xF59E0B)
