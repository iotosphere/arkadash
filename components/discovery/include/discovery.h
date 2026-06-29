/**
 * @file discovery.h
 * @brief UDP broadcast-based agent_server IP keşfi.
 *
 * Mimari: agent_server.py her 5 saniyede "ARKADASH:<ip>" string'ini
 * 255.255.255.255:53000'a broadcast eder. P4 boot'ta bu porta dinler,
 * ilk geçerli broadcast'te IP'yi alır ve WebSocket URI'yi dinamik oluşturur.
 *
 * Avantaj: agent_server'ın IP'si reboot sonrası değişse bile P4 otomatik
 * öğrenir. mDNS / Bonjour gerekmez (kullanıcının macOS'unda broken).
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* UDP broadcast portu — agent_server ile aynı olmalı */
#define DISCOVERY_PORT       53000

/* Broadcast mesaj formatı: "ARKADASH:<ip>[:<port>]" */
#define DISCOVERY_PREFIX     "ARKADASH:"

/* Max IP uzunluğu (xxx.xxx.xxx.xxx + null) */
#define DISCOVERY_IP_MAX_LEN 32

/* Discovery çalışmazsa fallback (son bilinen/kabul edilebilir bir IP)
 * Boot sırasında discovery başlamadan önce set edilebilir (örn. NVS'ten). */
void discovery_set_fallback(const char *ip);

/* Discovery task'ı başlat (UDP listener). FreeRTOS task olarak çalışır.
 * Döner ESP_OK veya hata kodu. */
esp_err_t discovery_start(void);

/* Şu anda bilinen server IP'sini döner (broadcast alındıysa onu,
 * yoksa fallback'ı). NULL-terminated string. */
const char* discovery_get_server_ip(void);

/* Boot sırasında discovery başladıktan sonra, ilk geçerli broadcast'i
 * beklemek için blocking çağrı. timeout_ms içinde ilk IP gelirse hemen
 * döner, timeout olursa fallback kullanılır.
 * NOT: FreeRTOS scheduler'ı bloklamaz, ms cinsinden uyur. */
void discovery_wait_for_first(uint32_t timeout_ms);

/* IP değişince çağrılacak callback. discovery'ten bağımsız thread context'inde
 * çağrılır (UDP task). WebSocket reconnect buradan tetiklenebilir. */
typedef void (*discovery_change_cb_t)(const char *new_ip);

/* Callback register. NULL geçilirse kaldırılır. */
void discovery_set_change_cb(discovery_change_cb_t cb);
