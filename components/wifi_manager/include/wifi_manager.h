#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_SCAN_RESULTS  20
#define WIFI_MANAGER_SSID_MAX_LEN      33
#define WIFI_MANAGER_PASS_MAX_LEN      65

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN];
    int8_t rssi;
    wifi_auth_mode_t authmode;
    uint8_t channel;
} wifi_manager_ap_info_t;

typedef struct {
    wifi_manager_ap_info_t aps[WIFI_MANAGER_MAX_SCAN_RESULTS];
    uint16_t count;
} wifi_manager_scan_result_t;

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t max_retry;
    bool auto_reconnect;
} wifi_manager_config_t;

typedef enum {
    WIFI_MANAGER_EVT_DISCONNECTED,
    WIFI_MANAGER_EVT_CONNECTING,
    WIFI_MANAGER_EVT_CONNECTED,
    WIFI_MANAGER_EVT_GOT_IP,
    WIFI_MANAGER_EVT_SCAN_DONE,
} wifi_manager_event_t;

typedef void (*wifi_manager_event_cb_t)(wifi_manager_event_t event, void *data, void *ctx);

esp_err_t wifi_manager_init(const wifi_manager_config_t *config);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_scan(wifi_manager_scan_result_t *result);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_get_ip(char *ip_str, size_t len);
esp_err_t wifi_manager_register_event_cb(wifi_manager_event_cb_t cb, void *ctx);
esp_err_t wifi_manager_deinit(void);

#ifdef __cplusplus
}
#endif
