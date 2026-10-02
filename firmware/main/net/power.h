#pragma once
/* Active hours vs quiet hours, from the local clock and touches. See board/quiet_hours.h. */

#include <stdbool.h>
#include <stdint.h>

#include "board/quiet_hours.h"

void power_init(void);                 /* sets the UK time zone */
power_mode_t power_mode(void);         /* ACTIVE or QUIET right now */
void power_note_touch(void);
uint32_t power_ms_since_touch(void);   /* large if never touched */           /* a touch holds the panel awake for awake_after_touch_s more */
bool power_local_hour(int *hour);      /* false until the clock has been set from the network */
