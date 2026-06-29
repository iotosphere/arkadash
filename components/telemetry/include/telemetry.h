/**
 * @file telemetry.h
 * @brief P4 → agent_server periyodik telemetry POST.
 *
 * Mimari: agent_server `:8088/api/telemetry` P4 firmware'in periyodik
 * (varsayılan 10s) POST'unu kabul eder. P4 buradan heap free, RSSI,
 * SSID, MAC, IP, Matter device count gibi yaşamsal bilgileri gönderir.
 * agent_server bu veriyi dict'te tutar ve `/api/health` response'unda
 * `p4_telemetry: [...]` olarak UI'a (FletApp) sunar.
 *
 * HTTP client: `esp_http_client`. JSON: elle (cJSON/kullanma yok — küçük
 * payload). String buffer 512 byte yeterli.
 *
 * Planlanan genişletmeler (Phase 2B):
 *  - telemetry_set_matter_count() ile akıllı lamba örneklerinin
 *    `esp_matter::node::get_node_count()` raporlaması.
 *  - Grafana/Prometheus pushgateway (uzun vade).
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* POST periyodu. Agent server TTL'i 60s — yani < 60s öneriyoruz. */
#define TELEMETRY_PERIOD_S   10
#define TELEMETRY_URL_MAX    96

/* İlk POST'u ne kadar geciktirelim. WS bağlantısı önce kurulsun. */
#define TELEMETRY_START_DELAY_S  5

/* Telemetry modülünü başlat. FreeRTOS task olarak periyodik POST eder.
 * Server URL'i discovery üzerinden öğrenilir (varsayılan). */
esp_err_t telemetry_start(void);

/* İleride URL override için (örn. test harness). NULL = default. */
void telemetry_set_server_url(const char *url);

/* Phase 2B: Matter device sayısını dışarıdan set et (esp-matter'dan
 * çağrılır). Şimdilik 0 kalır. */
void telemetry_set_matter_count(int n);

#ifdef __cplusplus
}
#endif
