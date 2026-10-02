#include "ui/train_ui.h"

#include <stdio.h>
#include <string.h>

#include "ui/theme.h"

#define W 320
#define H 172

static lv_color_t tone_color(tone_t t)
{
    switch (t) {
    case TONE_OK:   return COL_OK;
    case TONE_WARN: return COL_WARN;
    case TONE_BAD:  return COL_BAD;
    default:        return COL_DIM;
    }
}

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* One line of text in a fixed box. Overlong text ends in "..." (honest truncation, never reworded). */
static lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int w, lv_text_align_t al)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, s);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, lv_font_get_line_height(f));
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, al, 0);
    return l;
}

static void strike(lv_obj_t *l) { lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_STRIKETHROUGH, 0); }

static void reset(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
}

/* A small labelled chip. Width comes from the font metrics, not from the label: LVGL has not laid
 * the label out yet, and lv_obj_get_width() on an unlaid-out content-sized label returns a huge
 * placeholder (this once painted a full-width bar). `right` is the chip's right edge; returns its
 * left edge. border < 0 means no border. */
static int chip_draw(lv_obj_t *p, const char *t, lv_color_t bg, lv_color_t fg, int border_hex, int right, int top, int h)
{
    const lv_font_t *font = &lv_font_montserrat_12;
    int tw = lv_txt_get_width(t, strlen(t), font, 0, LV_TEXT_FLAG_NONE);
    int cw = tw + 12;
    lv_obj_t *c = rect(p, right - cw, top, cw, h, bg);
    lv_obj_set_style_radius(c, 2, 0);
    if (border_hex >= 0) {
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, lv_color_hex((uint32_t)border_hex), 0);
    }
    lv_obj_t *l = text(c, t, font, fg, 0, 0, tw + 2, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    return right - cw;
}

/* Freshness chip: text AND colour, so it survives colour-blind viewing. Stale and timetable use a
 * dark chip with a border and bright text: thin black-on-amber text washed out on the real panel. */
static int chip(lv_obj_t *p, freshness_t f, uint32_t age_s, int right, int top, int h, bool on_band)
{
    char t[16];
    board_chip_text(f, age_s, t, sizeof t);
    if (on_band)              return chip_draw(p, t, lv_color_black(), COL_CHIP_LIVE_FG, -1, right, top, h);
    if (f == FRESH_TIMETABLE) return chip_draw(p, t, COL_CHIP_SCHED_BG, COL_CHIP_SCHED_FG, 0x888888, right, top, h);
    if (f == FRESH_STALE)     return chip_draw(p, t, lv_color_hex(0x3A2A00), COL_WARN, 0xFBBF24, right, top, h);
    return chip_draw(p, t, COL_CHIP_LIVE_BG, COL_CHIP_LIVE_FG, -1, right, top, h);
}

/* Position dots: which of the swipeable directions is showing. Drawn right-aligned to `right`. */
static int dots(lv_obj_t *p, int count, int index, int right, int top)
{
    if (count < 2) return right;
    const int d = 7, gap = 6;
    int x = right - (count * d + (count - 1) * gap);
    for (int i = 0; i < count; i++) {
        lv_obj_t *o = rect(p, x + i * (d + gap), top, d, d, i == index ? COL_TEXT : lv_color_hex(0x555555));
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    }
    return x;
}

/* Header: "<station> - <direction>" on the left, freshness chip on the right, position dots between.
 * The chip is drawn first so the dots and title can use exactly the space it leaves. */
static void header_common(lv_obj_t *p, const char *station, const char *label, int dir_index, int dir_count,
                          lv_color_t bg, const char *fixed_chip, freshness_t f, uint32_t age_s)
{
    rect(p, 0, 0, W, 22, bg);
    rect(p, 0, 22, W, 1, lv_color_hex(0x333333));
    int chip_left = fixed_chip ? chip_draw(p, fixed_chip, COL_CHIP_SCHED_BG, COL_CHIP_SCHED_FG, 0x888888, W - 4, 2, 18)
                               : chip(p, f, age_s, W - 4, 2, 18, false);
    int dots_left = dots(p, dir_count, dir_index, chip_left - 8, 7);
    char t[56];
    snprintf(t, sizeof t, "%s - %s", station, label[0] ? label : "All trains");
    text(p, t, &lv_font_montserrat_16, COL_TEXT, 6, 1, dots_left - 6 - (dir_count > 1 ? 8 : 0), LV_TEXT_ALIGN_LEFT);
}

static void header(lv_obj_t *p, const board_t *b, freshness_t f, uint32_t age_s)
{
    header_common(p, b->station, b->label, b->dir_index, b->dir_count,
                  f == FRESH_STALE ? COL_HDR_STALE : COL_HDR_BG, NULL, f, age_s);
}

void train_ui_board(lv_obj_t *screen, const board_t *b, uint32_t age_s)
{
    reset(screen);
    freshness_t f = board_freshness(b, age_s);
    header(screen, b, f, age_s);

    int rows = b->count < 3 ? b->count : 3;
    for (int i = 0; i < rows; i++) {
        const board_service_t *s = &b->svc[i];
        int y = 23 + i * 44;   /* row = 44 px: 20 px destination, 14 px detail, 1 px rule */
        char detail[96];
        tone_t tone = board_row_detail(f, s, detail, sizeof detail);
        bool canc = s->state == SVC_CANCELLED;
        /* Stale rows keep times visible but grey: the screen must not look live. */
        lv_color_t main_col = canc ? COL_BAD : (f == FRESH_STALE ? COL_DIM : COL_TEXT);

        lv_obj_t *t = text(screen, s->std, &lv_font_montserrat_24, main_col, 6, y + 3, 68, LV_TEXT_ALIGN_LEFT);
        lv_obj_t *d = text(screen, s->dest, &lv_font_montserrat_20, main_col, 78, y + 3, 238, LV_TEXT_ALIGN_LEFT);
        if (canc) { strike(t); strike(d); }
        text(screen, detail, &lv_font_montserrat_14, tone_color(tone), 78, y + 26, 238, LV_TEXT_ALIGN_LEFT);
        rect(screen, 0, y + 43, W, 1, COL_RULE);
    }

    char foot[64];
    tone_t ft = board_footer(b, f, foot, sizeof foot);
    if (foot[0]) text(screen, foot, &lv_font_montserrat_12, tone_color(ft), 6, 157, 308, LV_TEXT_ALIGN_LEFT);
}

void train_ui_takeover(lv_obj_t *screen, const board_t *b, uint32_t age_s, takeover_t kind)
{
    reset(screen);
    freshness_t f = board_freshness(b, age_s);
    const board_service_t *s = &b->svc[0];
    char label[24], big[8], detail[96], dest[56], next[64];
    bool red = kind == TAKEOVER_CANCELLED;
    const char *pl = s->plat, *pw = s->plat_was;

    if (kind == TAKEOVER_CANCELLED) {
        snprintf(label, sizeof label, "CANCELLED");
        snprintf(big, sizeof big, "%s", s->std);
        snprintf(detail, sizeof detail, s->why[0] ? "Reason: %s" : "No reason given", s->why);
        snprintf(dest, sizeof dest, "%s", s->dest);
    } else if (kind == TAKEOVER_DELAYED) {
        snprintf(label, sizeof label, "DELAYED %u MIN", s->late);
        snprintf(big, sizeof big, "%s", s->etd);
        snprintf(detail, sizeof detail, s->why[0] ? "Was %s - %s" : "Was %s", s->std, s->why);
        snprintf(dest, sizeof dest, "%s", s->dest);
    } else {
        snprintf(label, sizeof label, "PLATFORM CHANGE");
        snprintf(big, sizeof big, "P%s", pl);
        snprintf(detail, sizeof detail, "Was platform %s - %s departure", pw, s->std);
        snprintf(dest, sizeof dest, "%s %s", s->std, s->dest);
    }
    board_next_line(b, f, next, sizeof next);

    rect(screen, 0, 0, W, 34, red ? COL_BAND_RED : COL_BAND_AMBER);
    int chip_left = chip(screen, f, age_s, W - 6, 8, 18, true);
    /* The label may use everything left of the chip ("PLATFORM CHANGE" needs ~230 px at 22 px). */
    text(screen, label, &lv_font_montserrat_22, red ? lv_color_white() : lv_color_black(), 8, 5, chip_left - 8 - 6, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *bg = text(screen, big, &lv_font_montserrat_48, red ? COL_BAD : COL_WARN, 8, 36, 304, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *dl = text(screen, dest, &lv_font_montserrat_22, red ? COL_BAD : COL_TEXT, 8, 96, 304, LV_TEXT_ALIGN_LEFT);
    if (red) { strike(bg); strike(dl); }
    text(screen, detail, &lv_font_montserrat_14, red ? COL_BAD : COL_WARN, 8, 124, 304, LV_TEXT_ALIGN_LEFT);

    rect(screen, 0, 150, W, 22, COL_HDR_BG);
    text(screen, next, &lv_font_montserrat_14, COL_TEXT, 8, 153, 304, LV_TEXT_ALIGN_LEFT);
}

void train_ui_empty(lv_obj_t *screen, const board_t *b, uint32_t age_s)
{
    reset(screen);
    freshness_t f = board_freshness(b, age_s);
    header(screen, b, f, age_s);
    text(screen, "No trains", &lv_font_montserrat_24, COL_TEXT, 0, 50, W, LV_TEXT_ALIGN_CENTER);
    text(screen, b->label[0] ? b->label : "All trains", &lv_font_montserrat_18, COL_TEXT, 0, 84, W, LV_TEXT_ALIGN_CENTER);
    text(screen, "None expected in the next 2 hours", &lv_font_montserrat_16, COL_DIM, 0, 108, W, LV_TEXT_ALIGN_CENTER);
}

void train_ui_status(lv_obj_t *screen, const char *station, const char *label, int dir_index, int dir_count,
                     const char *line1, const char *line2)
{
    reset(screen);
    header_common(screen, station, label, dir_index, dir_count, COL_HDR_BG, "NO DATA", FRESH_LIVE, 0);
    text(screen, line1, &lv_font_montserrat_24, COL_TEXT, 0, 56, W, LV_TEXT_ALIGN_CENTER);
    text(screen, line2, &lv_font_montserrat_16, COL_DIM, 6, 96, W - 12, LV_TEXT_ALIGN_CENTER);
}
