#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t max_retry;
} wifi_station_config_t;

esp_err_t wifi_station_init(const wifi_station_config_t *config);
bool wifi_station_is_connected(void);
esp_err_t wifi_station_disconnect(void);
esp_err_t wifi_station_deinit(void);
