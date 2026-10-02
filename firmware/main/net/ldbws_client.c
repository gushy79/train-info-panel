#include "net/ldbws_client.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "board/settings.h"
#include "esp_log.h"

#define BASE_URL "https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepartureBoard/"
/* The gateway returned 403 to Python's default User-Agent but accepted curl's; always identify. */
#define USER_AGENT "train-info-panel/0.1 (personal departure board)"

static const char *TAG = "ldbws_client";

typedef struct { char *body; size_t cap; size_t len; bool overflow; } sink_t;

static esp_err_t on_http_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        sink_t *s = e->user_data;
        if (s->len + (size_t)e->data_len + 1 > s->cap) { s->overflow = true; return ESP_OK; }
        memcpy(s->body + s->len, e->data, e->data_len);
        s->len += e->data_len;
    }
    return ESP_OK;
}

static bool crs_ok(const char *s)
{
    return s && strlen(s) == 3 && isupper((unsigned char)s[0]) && isupper((unsigned char)s[1]) && isupper((unsigned char)s[2]);
}

esp_err_t ldbws_fetch(const char *api_key, const char *station, const char *filter_crs, int rows,
                      char *body, size_t cap, size_t *len, int *http_status)
{
    *len = 0;
    *http_status = 0;
    if (!api_key || !api_key[0] || !crs_ok(station) || (filter_crs && !crs_ok(filter_crs)) || rows < 1 || rows > 10)
        return ESP_ERR_INVALID_ARG;

    char url[256];
    int n = filter_crs ? snprintf(url, sizeof url, BASE_URL "%s?numRows=%d&filterCrs=%s&filterType=to", station, rows, filter_crs)
                       : snprintf(url, sizeof url, BASE_URL "%s?numRows=%d", station, rows);
    if (n <= 0 || n >= (int)sizeof url) return ESP_ERR_INVALID_SIZE;

    sink_t sink = { .body = body, .cap = cap };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = (int)(settings()->request_timeout_s * 1000),
        .event_handler = on_http_event,
        .user_data = &sink,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* verify the server; no insecure fallback */
        .disable_auto_redirect = true,                /* never follow a redirect with the key attached */
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(c, "x-apikey", api_key);
    esp_http_client_set_header(c, "User-Agent", USER_AGENT);
    esp_http_client_set_header(c, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(c);
    *http_status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) { ESP_LOGW(TAG, "request failed: %s", esp_err_to_name(err)); return err; }
    if (sink.overflow) { ESP_LOGW(TAG, "response larger than %u bytes, rejected", (unsigned)cap); return ESP_ERR_NO_MEM; }
    body[sink.len] = '\0';
    *len = sink.len;
    ESP_LOGI(TAG, "HTTP %d, %u bytes", *http_status, (unsigned)sink.len);
    return ESP_OK;
}
