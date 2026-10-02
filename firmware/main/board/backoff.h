#pragma once
/* Poll scheduling: modest when healthy, bounded and growing when not. No hammering a service that
 * is rejecting us. Pure logic, host-tested. */

#include <stdint.h>

#define BACKOFF_MAX_S 300        /* ordinary failures never wait longer than this */
#define BACKOFF_AUTH_S 900       /* 401/403: our key is being refused; retrying quickly cannot help */
#define BACKOFF_RATELIMIT_S 600  /* 429 */

/* Seconds to wait before retrying after a failure (the success intervals are settings; see settings.h). failures >= 1.
 * http_status is the last response status (0 if there was none, e.g. timeout or Wi-Fi down). */
uint32_t backoff_next_delay_s(uint32_t consecutive_failures, int http_status);
