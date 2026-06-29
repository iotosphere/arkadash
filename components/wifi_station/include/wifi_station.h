#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t max_retry;
} wifi_station_config_t;

/* ==== Init / status ==== */

esp_err_t wifi_station_init(const wifi_station_config_t *config);
bool wifi_station_is_connected(void);
esp_err_t wifi_station_disconnect(void);
esp_err_t wifi_station_deinit(void);

/* ==== NVS credential storage (fabrika reset sonrası dahi çalışır) ====
 *
 * wifi_cred_save(): SSID + password'u NVS'e kalıcı yazar.
 *   Sonraki boot'ta wifi_station_init() bu değerleri otomatik okur.
 *   NULL password açık ağ (WIFI_AUTH_OPEN) için kullanılır.
 *
 * wifi_cred_load(): NVS'ten okur. Bulursa *ssid ve *pass'i doldurur
 *   (her biri için ayrı buffer). Bulamazsa ESP_ERR_NOT_FOUND döner.
 *   *ssid ve *pass NULL olabilir, o zaman default fallback kullanılır.
 *
 * wifi_cred_clear(): NVS'ten siler (fabrika reset — provisioning yeniden başlar). */

#define WIFI_NVS_NAMESPACE     "wifi_cred"
#define WIFI_NVS_KEY_SSID      "ssid"
#define WIFI_NVS_KEY_PASS      "pass"
#define WIFI_SSID_MAX_LEN       32   /* WiFi standardı: 32 byte */
#define WIFI_PASS_MAX_LEN       64   /* WPA2-PSK max 63 + null */

esp_err_t wifi_cred_save(const char *ssid, const char *password);
esp_err_t wifi_cred_load(char *ssid_out, size_t ssid_len,
                          char *pass_out, size_t pass_len);
esp_err_t wifi_cred_clear(void);
bool wifi_cred_exists(void);

/* Settings ekranındaki "WiFi Sıfırla" butonu için kolaylık:
 * 1) NVS'teki credential'ı sil
 * 2) 500ms bekle (NVS commit için)
 * 3) esp_restart() — provisioning mode otomatik açılır
 *
 * UI button event handler'ından direkt çağrılabilir. */
void wifi_reset_and_reboot(void);
