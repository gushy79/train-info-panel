#pragma once
#include <stddef.h>

#include "esp_err.h"

/* One bounded HTTPS GET of a departure board. `body` receives at most cap-1 bytes plus a NUL;
 * a longer response is rejected (ESP_ERR_NO_MEM), not truncated. Station and filter must be three
 * capital letters. The key is sent only as the x-apikey header, to the RDM host, with the server
 * certificate verified against the bundled CA list. */
esp_err_t ldbws_fetch(const char *api_key, const char *station, const char *filter_crs, int rows,
                      char *body, size_t cap, size_t *len, int *http_status);
