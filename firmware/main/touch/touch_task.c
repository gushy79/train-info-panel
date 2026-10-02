#include "touch/touch_task.h"

#include "board/swipe.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/poller.h"
#include "net/power.h"
#include "touch/axs5106.h"

#define POLL_MS 20

/* The controller reports native portrait coordinates (x 0..171 across, y 0..319 down the long side).
 * The screen runs rotated into landscape, so the long axis is the screen's horizontal one. These two
 * flags were set from observation on the real panel (swiping works left/right as expected). */
#define LANDSCAPE_X_FROM_NATIVE_Y 1
#define FLIP_LANDSCAPE_X 0

static const char *TAG = "touch";

static void touch_task(void *arg)
{
    (void)arg;
    swipe_tracker_t tracker = { 0 };
    int16_t last_x = 0;
    bool was_touching = false;

    for (;;) {
        uint16_t rx = 0, ry = 0;
        bool touching = axs5106_read(&rx, &ry);
        int16_t sx = 0, sy = 0;
        if (touching) {
            int16_t along = LANDSCAPE_X_FROM_NATIVE_Y ? (int16_t)ry : (int16_t)rx;
            int16_t across = LANDSCAPE_X_FROM_NATIVE_Y ? (int16_t)rx : (int16_t)ry;
            sx = FLIP_LANDSCAPE_X ? (int16_t)(319 - along) : along;
            sy = across;
            last_x = sx;
        }
        if (touching && !was_touching) {
            /* Any touch holds the panel awake; a touch that wakes it from quiet hours also forces a refresh. */
            bool was_quiet = power_mode() == POWER_QUIET;
            power_note_touch();
            if (was_quiet) {
                ESP_LOGI(TAG, "touch: waking from quiet hours");
                poller_refresh_now();
            }
        }
        was_touching = touching;
        swipe_t g = swipe_update(&tracker, touching, sx, sy, (uint32_t)(esp_timer_get_time() / 1000));
        if (g != SWIPE_NONE) {
            ESP_LOGI(TAG, "swipe %s (ended near x=%d)", g == SWIPE_LEFT ? "left" : "right", last_x);
            poller_step_direction(g == SWIPE_LEFT ? +1 : -1);   /* swipe left = next, like turning a page */
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void touch_task_start(void)
{
    if (axs5106_init() != ESP_OK) {
        ESP_LOGW(TAG, "no touch controller: swipe to change direction is unavailable");
        return;   /* the panel works without touch */
    }
    xTaskCreate(touch_task, "touch", 3072, NULL, 2, NULL);
}
