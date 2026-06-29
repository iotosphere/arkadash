/**
 * @file wifi_provisioning.h
 * @brief WiFi SoftAP provisioning — telefondan browser ile WiFi ayarı gir.
 *
 * Kullanım akışı:
 *   1. P4 boot → NVS'te SSID yoksa bu modül çağrılır
 *   2. P4 SoftAP açar: SSID="Arkadash-Setup", authmode=OPEN
 *   3. Telefonla "Arkadash-Setup" ağına bağlan
 *   4. Telefonun tarayıcısı 192.168.4.1'i açar (captive portal otomatik de olur)
 *   5. Form: SSID + password gir → Submit
 *   6. P4 form verilerini NVS'e yazar, 2 saniye sonra esp_restart()
 *   7. Reboot sonrası normal akış: NVS'ten oku, STA modunda bağlan
 *
 * Bu akışın alternatifi yok — BLE provisioning kullanmıyoruz (BLE+app gerekir,
 * telefonda uygulama istemeyiz).
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_PROV_AP_SSID       "Arkadash-Setup"
/* Authmode: açık (password yok) — kullanıcı kabul etsin diye friction yok.
 * Gerekirse WIFI_AUTH_WPA2_PSK + password ekle. */
#define WIFI_PROV_AP_IP          "192.168.4.1"
#define WIFI_PROV_AP_NETMASK     "255.255.255.0"
#define WIFI_PROV_AP_GATEWAY     "192.168.4.1"   /* self */
#define WIFI_PROV_CHANNEL        6
#define WIFI_PROV_MAX_CONN       4

/* SoftAP modunu başlat + HTTP server kur. NVS'e yazılmış bir WiFi yoksa
 * çağrılır. Bu fonksiyon geri dönmez — kullanıcı credential girip POST edince
 * otomatik restart eder. */
esp_err_t wifi_provisioning_start(void);

/* Provisioning modundan çık (test veya manuel cancel için). Restart etmez. */
void wifi_provisioning_stop(void);

/* Aktif mi? */
bool wifi_provisioning_is_active(void);