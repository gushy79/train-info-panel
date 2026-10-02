#pragma once
/*
 * Background task: Wi-Fi + time sync + a request to LDBWS about once a minute, with bounded
 * timeouts and backoff, keeping the last good board and how old it is. No server runs on the
 * device and nothing on the network can talk to it; it only makes outbound requests.
 */

#include <stdbool.h>
#include <stdint.h>

#include "board/board_logic.h"

typedef enum {
    NET_NO_CREDS,   /* nothing provisioned in NVS */
    NET_STARTING,   /* no board yet, still trying */
    NET_OK,         /* last request succeeded */
    NET_FAILING,    /* last request failed (a cached board may still exist) */
} net_state_t;

typedef struct {
    net_state_t state;
    bool have_board;
    board_t board;        /* the last good board */
    uint32_t age_s;       /* seconds since the provider generated it, as of now */
    uint32_t generation;  /* increments on every successful fetch */
    char reason[48];      /* short human reason for the latest failure ("" if none) */
    char station_name[24];
    char label[17];       /* current direction's label */
    uint8_t dir_index;    /* which of dir_count directions is selected */
    uint8_t dir_count;
} net_snapshot_t;

void poller_start(void);

/* Move to the next (+1) or previous (-1) direction, wrapping. Every direction keeps its own cached
 * board, refreshed in the background, so the new one shows at once (with its true age on the chip) and
 * is refreshed straight away if it is more than a few seconds old. One direction's trains are never
 * shown under another's label. The choice is remembered across reboots. Safe from any task. */
void poller_step_direction(int step);
void poller_snapshot(net_snapshot_t *out);

/* Make the on-screen direction due now and wake the poller (a touch while the panel was asleep). */
void poller_refresh_now(void);

/* Forget every cached board. Called when quiet hours begin: by morning they are hours old and
 * meaningless, and the first thing shown on waking must not be yesterday's trains. */
void poller_clear_cache(void);
