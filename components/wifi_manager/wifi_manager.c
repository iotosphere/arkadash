#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_connected = false;
static bool s_initialized = false;
static uint8_t s_max_retry = 5;
static int s_retry_num = 0;
static bool s_auto_reconnect = true;
static char s_ssid[WIFI_MANAGER_SSID_MAX_LEN] = {0};
static char s_password[WIFI_MANAGER_PASS_MAX_LEN] = {0};

static wifi_manager_event_cb_t s_event_cb = NULL;
static void *s_event_ctx = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_SCAN_DONE_BIT BIT2

static void notify_event(wifi_manager_event_t event, void *data)
{
    if (s_event_cb) {
        s_event_cb(event, data, s_event_ctx);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
        notify_event(WIFI_MANAGER_EVT_DISCONNECTED, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        notify_event(WIFI_MANAGER_EVT_DISCONNECTED, NULL);
        if (s_auto_reconnect && s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Yeniden bağlanma: %d/%d", s_retry_num, s_max_retry);
            notify_event(WIFI_MANAGER_EVT_CONNECTING, NULL);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP alındı: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        notify_event(WIFI_MANAGER_EVT_GOT_IP, event_data);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Tarama tamamlandı");
        notify_event(WIFI_MANAGER_EVT_SCAN_DONE, NULL);
        xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
    }
}

esp_err_t wifi_manager_init(const wifi_manager_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Zaten başlatılmış");
        return ESP_OK;
    }

    if (!config || !config->ssid) {
        ESP_LOGE(TAG, "Geçersiz yapılandırma");
        return ESP_ERR_INVALID_ARG;
    }

    s_max_retry = config->max_retry > 0 ? config->max_retry : 5;
    s_auto_reconnect = config->auto_reconnect;
    s_connected = false;
    s_retry_num = 0;

    strlcpy(s_ssid, config->ssid, sizeof(s_ssid));
    if (config->password) {
        strlcpy(s_password, config->password, sizeof(s_password));
    }

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Event group oluşturulamadı");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi Manager başlatıldı");
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Önce init çağrılmalı");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_scan_done;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &event_handler, NULL, &instance_scan_done));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    if (s_password[0]) {
        strlcpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    notify_event(WIFI_MANAGER_EVT_CONNECTING, NULL);
    ESP_LOGI(TAG, "WiFi başlatıldı, bağlanıyor: %s", s_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid) {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
    }
    if (password) {
        strlcpy(s_password, password, sizeof(s_password));
    }

    s_retry_num = 0;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    if (s_password[0]) {
        strlcpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    notify_event(WIFI_MANAGER_EVT_CONNECTING, NULL);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    s_connected = false;
    s_auto_reconnect = false;
    notify_event(WIFI_MANAGER_EVT_DISCONNECTED, NULL);
    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_scan(wifi_manager_scan_result_t *result)
{
    if (!s_initialized || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tarama başlatılamadı: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > WIFI_MANAGER_MAX_SCAN_RESULTS) {
        ap_count = WIFI_MANAGER_MAX_SCAN_RESULTS;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    result->count = ap_count;
    for (uint16_t i = 0; i < ap_count; i++) {
        strlcpy(result->aps[i].ssid, (char *)ap_records[i].ssid, WIFI_MANAGER_SSID_MAX_LEN);
        result->aps[i].rssi = ap_records[i].rssi;
        result->aps[i].authmode = ap_records[i].authmode;
        result->aps[i].channel = ap_records[i].primary;
    }

    free(ap_records);
    ESP_LOGI(TAG, "%d ağ bulundu", ap_count);
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t len)
{
    if (!ip_str || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret == ESP_OK) {
        snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    }
    return ret;
}

esp_err_t wifi_manager_register_event_cb(wifi_manager_event_cb_t cb, void *ctx)
{
    s_event_cb = cb;
    s_event_ctx = ctx;
    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void)
{
    s_connected = false;
    s_initialized = false;
    s_event_cb = NULL;
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    return ESP_OK;
}
