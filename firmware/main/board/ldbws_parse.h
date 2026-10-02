#pragma once
/*
 * LDBWS station-board JSON -> board_t. The on-device twin of adapter/normalise.py: the host test
 * (firmware/test/host) feeds both the same raw boards and requires identical results.
 * No ESP-IDF dependency (only cJSON), so it runs on the host under ASan/UBSan.
 *
 * Input is untrusted network data: every field is type-checked, every copy is bounded, and a
 * failed parse leaves `out` untouched.
 */

#include <stddef.h>
#include <stdint.h>

#include "board/board_logic.h"

/* Platform of each service on the previous poll, keyed by LDBWS serviceID, to detect changes
 * (LDBWS reports only the current platform). Updated in place on every successful parse. */
typedef struct { char id[24]; char plat[4]; } prev_plat_t;
typedef struct { prev_plat_t e[BOARD_MAX_SERVICES]; uint8_t n; } prev_platforms_t;

typedef enum {
    LDB_OK = 0,
    LDB_ERR_JSON,      /* not valid JSON */
    LDB_ERR_SHAPE,     /* valid JSON but not a station board */
    LDB_UNAVAILABLE,   /* areServicesAvailable=false: provider has no board (not an empty board) */
} ldb_result_t;

/* label: header text for this direction ("Southbound"), or "" for all trains.
 * generated: provider timestamp as Unix seconds, or 0 if absent or without a UTC offset.
 * prev may be NULL. */
ldb_result_t ldbws_parse(const char *json, size_t len, const char *label,
                         board_t *out, int64_t *generated, prev_platforms_t *prev);
