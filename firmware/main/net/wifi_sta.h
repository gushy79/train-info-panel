#pragma once
#include <stdbool.h>

/* Join a WPA2/WPA3 network and keep rejoining. Never logs the password. */
void wifi_sta_start(const char *ssid, const char *password);
bool wifi_sta_connected(void);
