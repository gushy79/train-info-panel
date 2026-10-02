#include "net/poller.h"

#include <string.h>
#include <time.h>

#include "board/backoff.h"
#include "board/ldbws_parse.h"
#include "board/schedule.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/ldbws_client.h"
#include "board/settings.h"
#include "net/settings_nvs.h"
#include "net/power.h"
#include "net/wifi_sta.h"
#include "nvs.h"
#include "nvs_flash.h"

#define RX_CAP (12 * 1024)      /* bigger than any real board (~2-4 KB); anything larger is rejected */
#define WIFI_WAIT_S 5
#define VALID_UNIX_TIME 1700000000   /* anything earlier means the clock has not been set */
#define STAGGER_S 7                  /* first fetch of each background direction, spread out after boot */

static const char *TAG = "poller";

/* One cache slot per direction. A response always lands in the slot it was requested for, so a board
 * can never appear under another direction's label, and a swipe shows cached data instantly. */
typedef struct {
    board_t board;
    bool have;
    uint32_t generation;
    int64_t fetched_mono_s;     /* esp_timer seconds at the last good fetch */
    uint32_t base_age_s;        /* provider age at that moment */
    prev_platforms_t prev;
    uint32_t failures;
    char reason[48];
    int64_t due_s;              /* when this slot may next be fetched */
} slot_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static slot_t s_slot[MAX_DIRECTIONS];
static volatile uint8_t s_dir;             /* direction on screen */
static volatile bool s_no_creds;
static volatile bool s_wifi_down;
static char s_station_name[24];

typedef struct {
    char ssid[33], pass[65], key[96];
} creds_t;

static int64_t mono_s(void) { return esp_timer_get_time() / 1000000; }

static bool nvs_str(nvs_handle_t h, const char *key, char *out, size_t n)
{
    size_t len = n;
    return nvs_get_str(h, key, out, &len) == ESP_OK && out[0];
}

/* The selected direction is the one piece of user state worth keeping across a reboot. */
static uint8_t load_direction(void)
{
    nvs_handle_t h;
    uint8_t d = 0;
    if (nvs_open("state", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "direction", &d) != ESP_OK) d = 0;
        nvs_close(h);
    }
    return d < settings()->direction_count ? d : 0;
}

static void save_direction(uint8_t d)
{
    nvs_handle_t h;
    if (nvs_open("state", NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t cur = 255;
    nvs_get_u8(h, "direction", &cur);
    if (cur != d) { nvs_set_u8(h, "direction", d); nvs_commit(h); }   /* skip identical writes: flash wear */
    nvs_close(h);
}

/* Credentials come only from NVS (written by tools/provision.py). Required: wifi_ssid, wifi_pass, api_key. */
static bool load_creds(creds_t *c)
{
    memset(c, 0, sizeof *c);

    nvs_handle_t h;
    if (nvs_open("panel", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_str(h, "wifi_ssid", c->ssid, sizeof c->ssid) &&
              nvs_str(h, "wifi_pass", c->pass, sizeof c->pass) &&
              nvs_str(h, "api_key", c->key, sizeof c->key);
    nvs_close(h);
    return ok;
}

static uint32_t jitter(uint32_t seconds)
{
    return seconds >= 20 ? esp_random() % (seconds / 10) : 0;   /* up to +10 %: no lock-step polling */
}

static void apply_failure(slot_t *sl, const char *reason, int status, int64_t now)
{
    sl->failures++;
    snprintf(sl->reason, sizeof sl->reason, "%s", reason);
    uint32_t wait = backoff_next_delay_s(sl->failures, status);
    sl->due_s = now + wait + jitter(wait);
}

static void poll_task(void *arg)
{
    (void)arg;
    creds_t *c = calloc(1, sizeof *c);
    char *rx = malloc(RX_CAP);
    if (!c || !rx || !load_creds(c)) {
        ESP_LOGW(TAG, "not configured: run tools/provision.py");
        s_no_creds = true;
        free(c);
        free(rx);
        vTaskDelete(NULL);
        return;
    }
    snprintf(s_station_name, sizeof s_station_name, "%s", settings()->station_name);

    wifi_sta_start(c->ssid, c->pass);
    memset(c->pass, 0, sizeof c->pass);   /* the Wi-Fi driver has its own copy; drop ours */
    bool sntp_started = false;
    int64_t last_request_s = -MIN_REQUEST_GAP_S;

    int64_t t0 = mono_s();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0, k = 0; i < settings()->direction_count; i++)
        s_slot[i].due_s = (i == s_dir) ? t0 : t0 + (++k) * STAGGER_S;   /* the one on screen first */
    xSemaphoreGive(s_lock);

    bool was_quiet = false;
    for (;;) {
        /* Quiet hours: no requests at all. Wi-Fi stays up so a touch can refresh at once; the touch
         * marks the panel awake first, then nudges us, so the next pass sees active hours. */
        if (power_mode() == POWER_QUIET) {
            was_quiet = true;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30 * 1000));
            continue;
        }
        if (was_quiet) {
            was_quiet = false;   /* waking: the direction on screen is due now, the others follow */
            int64_t t = mono_s();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            for (int i = 0, k = 0; i < settings()->direction_count; i++)
                s_slot[i].due_s = (i == s_dir) ? t : t + (++k) * STAGGER_S;
            xSemaphoreGive(s_lock);
        }
        if (!wifi_sta_connected()) {
            s_wifi_down = true;
            vTaskDelay(pdMS_TO_TICKS(WIFI_WAIT_S * 1000));
            continue;
        }
        s_wifi_down = false;
        if (!sntp_started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            sntp_started = true;
        }

        int64_t due[MAX_DIRECTIONS];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < settings()->direction_count; i++) due[i] = s_slot[i].due_s;
        int visible = s_dir;
        xSemaphoreGive(s_lock);

        uint32_t wait = 0;
        int d = schedule_pick(due, settings()->direction_count, visible, mono_s(), last_request_s, &wait);
        if (wait > 0) {
            /* Sleep until due, or until a swipe wakes us to re-pick (the new direction may be due now). */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait * 1000));
            continue;
        }

        const direction_cfg_t *dir = &settings()->directions[d];
        size_t len = 0;
        int status = 0;
        last_request_s = mono_s();
        esp_err_t err = ldbws_fetch(c->key, settings()->station_crs, dir->toward[0] ? dir->toward : NULL, settings()->num_rows, rx, RX_CAP, &len, &status);
        int64_t now = mono_s();
        const char *reason = NULL;
        board_t b;
        int64_t generated = 0;
        prev_platforms_t next_prev;

        if (err != ESP_OK) {
            reason = err == ESP_ERR_NO_MEM ? "Response too large" : "No response from server";
        } else if (status == 401 || status == 403) {
            reason = "API key refused";
        } else if (status == 429) {
            reason = "Rate limited";
        } else if (status != 200) {
            reason = "Server error";
        } else {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            next_prev = s_slot[d].prev;
            xSemaphoreGive(s_lock);
            ldb_result_t r = ldbws_parse(rx, len, dir->label, &b, &generated, &next_prev);
            if (r != LDB_OK) reason = r == LDB_UNAVAILABLE ? "Station has no services" : "Unreadable response";
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        slot_t *sl = &s_slot[d];
        if (reason) {
            apply_failure(sl, reason, status, now);
            ESP_LOGW(TAG, "[%s] fetch failed (%s), failure %u", dir->label, reason, (unsigned)sl->failures);
        } else {
            time_t wall = time(NULL);
            uint32_t base_age = 0;
            if (generated > 0 && wall > VALID_UNIX_TIME && wall > generated) base_age = (uint32_t)(wall - generated);
            sl->board = b;
            sl->prev = next_prev;
            sl->have = true;
            sl->generation++;
            sl->failures = 0;
            sl->reason[0] = '\0';
            sl->fetched_mono_s = now;
            sl->base_age_s = base_age;
            uint32_t every = schedule_interval_s(d == s_dir);
            sl->due_s = now + every + jitter(every);
            ESP_LOGI(TAG, "[%s] board ok: %u services, age %us%s", dir->label, b.count, (unsigned)base_age,
                     d == s_dir ? "" : " (background)");
        }
        xSemaphoreGive(s_lock);
    }
}

void poller_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    /* Never erase NVS here: it holds the credentials. If it is unreadable, report not configured. */
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(e));
    if (e == ESP_OK) settings_load_from_nvs();   /* every user-adjustable value, from config.ini via provisioning */
    power_init();                                /* time zone, for the quiet-hours clock */
    s_dir = e == ESP_OK ? load_direction() : 0;
    snprintf(s_station_name, sizeof s_station_name, "%s", settings()->station_name);
    xTaskCreate(poll_task, "poll", 8192, NULL, 3, &s_task);
}

void poller_step_direction(int step)
{
    if (!s_lock || settings()->direction_count < 2) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t d = (uint8_t)((s_dir + settings()->direction_count + (step < 0 ? -1 : 1)) % settings()->direction_count);
    s_dir = d;
    /* The cached board for d shows immediately; refresh it now unless it is only seconds old. */
    slot_t *sl = &s_slot[d];
    sl->due_s = schedule_due_after_swipe(sl->due_s, sl->have, sl->fetched_mono_s, mono_s());
    xSemaphoreGive(s_lock);
    save_direction(d);
    ESP_LOGI(TAG, "direction -> %s", settings()->directions[d].label);
    if (s_task) xTaskNotifyGive(s_task);   /* re-pick now: the new direction may be due */
}

void poller_refresh_now(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_slot[s_dir].due_s = mono_s();
    xSemaphoreGive(s_lock);
    if (s_task) xTaskNotifyGive(s_task);
}

void poller_clear_cache(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < settings()->direction_count; i++) {
        s_slot[i].have = false;
        s_slot[i].board = (board_t){ 0 };
        s_slot[i].prev = (prev_platforms_t){ 0 };
        s_slot[i].failures = 0;
        s_slot[i].reason[0] = '\0';
        s_slot[i].generation++;
    }
    xSemaphoreGive(s_lock);
}

void poller_snapshot(net_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const slot_t *sl = &s_slot[s_dir];
    memset(out, 0, sizeof *out);
    snprintf(out->station_name, sizeof out->station_name, "%s", s_station_name);
    snprintf(out->label, sizeof out->label, "%s", settings()->directions[s_dir].label);
    out->dir_index = s_dir;
    out->dir_count = settings()->direction_count;
    out->generation = sl->generation;
    out->have_board = sl->have;
    if (sl->have) {
        out->board = sl->board;
        int64_t elapsed = mono_s() - sl->fetched_mono_s;
        out->age_s = sl->base_age_s + (uint32_t)(elapsed > 0 ? elapsed : 0);   /* ages with the monotonic clock, not wall time */
    }
    if (s_no_creds)                      out->state = NET_NO_CREDS;
    else if (s_wifi_down)                { out->state = sl->have ? NET_FAILING : NET_STARTING; snprintf(out->reason, sizeof out->reason, "Wi-Fi not connected"); }
    else if (sl->failures > 0)           { out->state = NET_FAILING; snprintf(out->reason, sizeof out->reason, "%s", sl->reason); }
    else                                 out->state = sl->have ? NET_OK : NET_STARTING;
    xSemaphoreGive(s_lock);
}
