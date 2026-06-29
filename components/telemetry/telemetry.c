/**
 * @file telemetry.c
 * @brief P4 → agent_server periyodik telemetry POST implementasyonu.
 */

#include "telemetry.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"

#include "discovery.h"   /* server IP'yi dinamik öğrenmek için */

static const char *TAG = "TELE";

/* Override edilebilir; default olarak agent_server URL'i */
static char s_server_url[TELEMETRY_URL_MAX] = {0};
static int  s_matter_count = 0;            /* Phase 2B için */
static TaskHandle_t s_task = NULL;
static volatile bool s_run = false;

/* Yardımcılar */
static const char* _ip_to_str(esp_ip4_addr_t *ip) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             esp_ip4_addr_get_byte(ip, 0), esp_ip4_addr_get_byte(ip, 1),
             esp_ip4_addr_get_byte(ip, 2), esp_ip4_addr_get_byte(ip, 3));
    return buf;
}

/* Tek bir POST. Hata olursa log + return. */
static void post_once(const char *url) {
    /* Telemetry JSON topla */
    char ssid[33] = {0};
    char mac_str[18] = {0};
    char ip_local[16] = {0};
    int  rssi = 0;
    uint32_t heap_free = 0;
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_s  = (uint32_t)(uptime_us / 1000000ULL);

    /* WiFi bağlıysa RSSI + SSID + IP al */
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
        strncpy(ssid, (const char*)ap_info.ssid, sizeof(ssid) - 1);
    } else {
        strncpy(ssid, "(disconnected)", sizeof(ssid) - 1);
    }

    /* STA MAC — kendi iface mac'imiz */
    uint8_t mac[6] = {0};
    if (esp_netif_get_mac(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
                          mac) != ESP_OK) {
        /* Default ifkey bulamazsa default sta iface'i dene */
        esp_netif_t *sta = esp_netif_get_default_netif();
        if (sta) esp_netif_get_mac(sta, mac);
    }
    snprintf(mac_str, sizeof(mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* STA local IP */
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) sta = esp_netif_get_default_netif();
    if (sta) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK
            && ip_info.ip.addr != 0) {
            strncpy(ip_local, _ip_to_str(&ip_info.ip), sizeof(ip_local) - 1);
        }
    }
    if (ip_local[0] == 0) strncpy(ip_local, "(no ip)", sizeof(ip_local) - 1);

    /* Heap free (sadece internal RAM — FreeRTOS task'ları için en kritik
     * alan; PSRAM'a bakmayız). */
    heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    /* JSON body — elle yaz, cJSON dependency'si istemiyoruz */
    char body[512];
    int body_len = snprintf(body, sizeof(body),
        "{"
            "\"heap_free\":%lu,"
            "\"rssi\":%d,"
            "\"ssid\":\"%s\","
            "\"mac\":\"%s\","
            "\"ip_local\":\"%s\","
            "\"uptime_s\":%lu,"
            "\"matter_count\":%d"
        "}",
        (unsigned long)heap_free, rssi, ssid, mac_str, ip_local,
        (unsigned long)uptime_s, s_matter_count);
    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        ESP_LOGW(TAG, "JSON body len=%d out of range", body_len);
        return;
    }

    /* HTTP POST */
    esp_http_client_config_t cfg = {
        .url          = url,
        .method       = HTTP_METHOD_POST,
        .timeout_ms   = 4000,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        ESP_LOGW(TAG, "http_client_init failed");
        return;
    }
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_post_field(h, body, body_len);

    esp_err_t err = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "POST %s ok (heap=%lu rssi=%d)",
                 url, (unsigned long)heap_free, rssi);
    } else {
        ESP_LOGW(TAG, "POST %s failed: err=%s status=%d",
                 url, esp_err_to_name(err), status);
    }
    esp_http_client_cleanup(h);
}

void telemetry_set_server_url(const char *url) {
    if (!url) return;
    strncpy(s_server_url, url, sizeof(s_server_url) - 1);
    s_server_url[sizeof(s_server_url) - 1] = 0;
}

void telemetry_set_matter_count(int n) {
    s_matter_count = n;
}

static void telemetry_task(void *arg) {
    /* İlk periyottan önce WS bağlantısı kurulsun */
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_START_DELAY_S * 1000));
    ESP_LOGI(TAG, "telemetry task started (period=%ds)", TELEMETRY_PERIOD_S);

    while (s_run) {
        /* URL güncel mi? discovery'den IP al. */
        char url[TELEMETRY_URL_MAX];
        if (s_server_url[0] != 0) {
            snprintf(url, sizeof(url), "%s", s_server_url);
        } else {
            const char *ip = discovery_get_server_ip();
            snprintf(url, sizeof(url),
                     "http://%s:8088/api/telemetry",
                     (ip && ip[0]) ? ip : "192.168.1.6");
        }

        post_once(url);

        /* Periyod bekle — vTaskDelay yerine parçalı uyku (sticky watchdog
         * için kısa adımlar). */
        for (int i = 0; i < TELEMETRY_PERIOD_S && s_run; ++i) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    vTaskDelete(NULL);
}

esp_err_t telemetry_start(void) {
    if (s_task) return ESP_OK;
    s_run = true;
    BaseType_t ok = xTaskCreate(
        telemetry_task, "telemetry",
        /* stack */ 4096,
        NULL, /* arg  */ 2,
        &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        s_run = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}
