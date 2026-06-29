
#include "wifi_station.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_station";

/* init() sırasında bağlantı için bekleme timeout (ms).
 * 15s içinde bağlanamazsa provisioning mode'a düş. */
#define WIFI_CONNECT_TIMEOUT_MS  15000

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_connected = false;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static uint8_t s_max_retry = 5;

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Yeniden bağlanma: %d/%d", s_retry_num, s_max_retry);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "WiFi bağlantısı kesildi");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP aldı: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_station_init(const wifi_station_config_t *config)
{
    if (!config || !config->ssid) {
        ESP_LOGE(TAG, "Geçersiz yapılandırma");
        return ESP_ERR_INVALID_ARG;
    }

    s_max_retry = config->max_retry > 0 ? config->max_retry : 5;
    s_connected = false;
    s_retry_num = 0;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_LOGI(TAG, "WiFi Config - SSID: %s", config->ssid);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
    if (config->password) {
        strlcpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi başlatıldı");

    /* 15 saniye içinde bağlanamazsa FAIL — provisioning mode'a geçiş için.
     * (Sonsuz portMAX_DELAY yerine sınırlı timeout — main.c buna göre
     * fallback stratejisi çalıştırır.) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✓ WiFi'ye bağlandı!");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "✗ WiFi bağlantısı başarısız!");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "✗ Beklenmeyen olay!");
        return ESP_FAIL;
    }
}

bool wifi_station_is_connected(void)
{
    return s_connected;
}

esp_err_t wifi_station_disconnect(void)
{
    s_connected = false;
    return esp_wifi_disconnect();
}

esp_err_t wifi_station_deinit(void)
{
    s_connected = false;
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        ret = esp_wifi_deinit();
    }
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    return ret;
}

/* ===================================================================
 * NVS credential storage — 2026-06-29
 *
 * Kullanıcı ilk açılışta (veya "WiFi Sıfırla" sonrası) credential girer
 * (SoftAP provisioning akışında veya hardcoded fallback). Bu fonksiyonlar
 * NVS flash partition'a kalıcı yazar, sonraki bootlarda oradan okunur.
 * Böylece firmware'de hardcoded SSID/PASS olmasına gerek kalmaz.
 *
 * NVS namespace: "wifi_cred"
 * Keys: "ssid" (≤32 byte), "pass" (≤64 byte, NULL = açık ağ)
 * =================================================================== */

esp_err_t wifi_cred_save(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) >= WIFI_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "wifi_cred_save: invalid ssid (len=%u)",
                 ssid ? (unsigned)strlen(ssid) : 0);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(h, WIFI_NVS_KEY_SSID, ssid);
    if (err == ESP_OK && password) {
        err = nvs_set_str(h, WIFI_NVS_KEY_PASS, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi cred saved: ssid='%s' (pass len=%u)",
                 ssid, password ? (unsigned)strlen(password) : 0);
    } else {
        ESP_LOGE(TAG, "wifi_cred_save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_cred_load(char *ssid_out, size_t ssid_len,
                          char *pass_out, size_t pass_len)
{
    if (!ssid_out || !pass_out || ssid_len < WIFI_SSID_MAX_LEN ||
        pass_len < WIFI_PASS_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* NOT_FOUND normal — ilk açılışta beklenir */
        return err;
    }

    /* SSID oku */
    size_t required = ssid_len;
    err = nvs_get_str(h, WIFI_NVS_KEY_SSID, ssid_out, &required);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }

    /* Password opsiyonel — açık ağlar için */
    required = pass_len;
    err = nvs_get_str(h, WIFI_NVS_KEY_PASS, pass_out, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Şifre kaydedilmemiş → açık ağ */
        pass_out[0] = '\0';
        err = ESP_OK;
    }

    nvs_close(h);
    return err;
}

esp_err_t wifi_cred_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "WiFi creds wiped (factory reset)");
    }
    return err;
}

bool wifi_cred_exists(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = 0;
    bool has = (nvs_get_str(h, WIFI_NVS_KEY_SSID, NULL, &len) == ESP_OK && len > 1);
    nvs_close(h);
    return has;
}

void wifi_reset_and_reboot(void)
{
    ESP_LOGW(TAG, "WiFi reset: NVS wiped, restarting...");
    esp_err_t err = wifi_cred_clear();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_cred_clear failed: %s", esp_err_to_name(err));
    }
    /* NVS commit + log flush için kısa bekleme */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
