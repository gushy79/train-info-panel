#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_lvgl_port.h"

#include "display/bsp_display.h"
#include "app/app_view.h"
#include "net/poller.h"
#include "spike/screen_cycle.h"
#include "touch/touch_task.h"
#include "esp_timer.h"

/* Landscape 320x172 only (CLAUDE.md). Display bring-up derived from Waveshare's demo package. */
#define LCD_H_RES 320
#define LCD_V_RES 172
#define LCD_DRAW_BUFF_HEIGHT 50
#define LCD_DRAW_BUFF_DOUBLE 1

static const char *TAG = "app_main";

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static lv_display_t *s_lvgl_disp = NULL;

#if !CONFIG_TRAIN_DEMO_CYCLE
static void view_timer_cb(lv_timer_t *t)
{
    app_view_tick(lv_scr_act(), (uint32_t)(esp_timer_get_time() / 1000));
    (void)t;
}
#endif

static esp_err_t app_lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 1024 * 10,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, 0, 34));
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    return s_lvgl_disp ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    bsp_spi_init();
    bsp_display_init(&s_io_handle, &s_panel_handle, LCD_H_RES * LCD_DRAW_BUFF_HEIGHT);

    ESP_ERROR_CHECK(app_lvgl_init());
#if !CONFIG_TRAIN_DEMO_CYCLE
    poller_start();      /* outbound-only: Wi-Fi + HTTPS client, no listening sockets */
    touch_task_start();  /* swipe left/right changes direction; the panel works without it */
#endif

    bsp_display_brightness_init();
    bsp_display_set_brightness(100);

    if (lvgl_port_lock(0)) {
        lv_obj_t *screen = lv_disp_get_scr_act(s_lvgl_disp);
#if CONFIG_TRAIN_DEMO_CYCLE
        screen_cycle_start(screen);
#else
        app_view_tick(screen, (uint32_t)(esp_timer_get_time() / 1000));
        lv_timer_create(view_timer_cb, 250, NULL);   /* quick enough that a touch wakes the screen promptly */
#endif
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "running");
}
