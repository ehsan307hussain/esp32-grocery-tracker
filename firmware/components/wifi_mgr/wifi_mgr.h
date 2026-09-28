#pragma once
#include "esp_err.h"

esp_err_t wifi_mgr_init(void);
esp_err_t wifi_mgr_connect_sta(const char *ssid, const char *pass, int timeout_ms);
