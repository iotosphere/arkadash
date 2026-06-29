/**
 * @file wifi_provisioning.c
 * @brief SoftAP provisioning — telefon browser ile WiFi credential gir.
 *
 * Akış:
 *   - Bu modül çağrıldığında `esp_wifi_set_mode(WIFI_MODE_APSTA)` yapar
 *     (hem AP hem STA — STA tarafı daha sonra bağlanacak ağı için hazır)
 *   - AP başlat: SSID="Arkadash-Setup", açık (şifresiz), 192.168.4.1/24
 *   - DNS server başlat (tüm *.x sorgularını 192.168.4.1'e yönlendir —
 *     iOS/Android captive portal otomatik açılır)
 *   - HTTP server 192.168.4.1:80'de
 *   - GET /  → HTML form (SSID + password input)
 *   - POST /submit → ssid/pass parse, NVS'e kaydet, 2 saniye sonra restart
 */

#include "wifi_provisioning.h"
#include "wifi_station.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_netif_defaults.h"

static const char *TAG = "WIFI_PROV";
static bool s_active = false;

/* ===========================================================
 * Embedded HTML form (max ~2 KB — flash'ta constant tutulur)
 *
 * Captive portal otomatik açılır, telefon browser'da görünür.
 * Form GET yerine POST kullanır → server'da credential kaydeder. */

static const char INDEX_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Arkadash WiFi Setup</title>"
"<style>"
"body{font-family:-apple-system,sans-serif;max-width:480px;margin:24px auto;padding:0 16px;color:#222}"
"h1{font-size:1.4em}.card{border:1px solid #ddd;border-radius:8px;padding:20px;margin-top:16px}"
"label{display:block;margin:14px 0 4px;font-weight:bold}"
"input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;font-size:16px}"
"button{background:#007aff;color:#fff;border:0;padding:12px 24px;border-radius:4px;font-size:16px;cursor:pointer;margin-top:18px}"
"button:hover{background:#0062cc}"
".info{background:#f5f5f5;border-radius:6px;padding:12px;font-size:0.85em;color:#666;margin-bottom:16px}"
"</style></head><body>"
"<h1>Arkadash WiFi Setup</h1>"
"<div class='info'>P4 restart olacak ve yeni WiFi'ye bağlanacak. Bu pencere kapanır.</div>"
"<form method='POST' action='/submit' class='card'>"
"<label>WiFi Agi (SSID)</label>"
"<input type='text' name='ssid' maxlength='32' required autofocus>"
"<label>Sifre (bos = acik ag)</label>"
"<input type='password' name='pass' maxlength='64'>"
"<button type='submit'>Kaydet ve Baglan</button>"
"</form></body></html>";

static const char SAVED_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Kaydedildi</title>"
"<meta http-equiv='refresh' content='5;url=http://192.168.4.1/'>"
"<style>body{font-family:-apple-system,sans-serif;max-width:480px;margin:60px auto;text-align:center}"
"h1{color:#34c759}</style></head><body>"
"<h1>Kaydedildi!</h1><p>P4 yeniden baslatiliyor... yeni WiFi'ye 5 saniye icinde baglanacak.</p>"
"<p style='color:#999'>Bu pencere kapanabilir.</p>"
"</body></html>";

/* ===========================================================
 * HTML form'u "ARKADASH:<ip>" data ile response eder
 * (P4'ün ekranında da gösterilebilir — fallback).
 * Şimdilik basit HTML form, dinamik bilgi yok. */

/* GET / — form HTML */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

/* GET /submit → GET handler'a yönlendir */
static esp_err_t submit_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

/* POST /submit → parse form, NVS'e kaydet, restart */
static esp_err_t submit_post_handler(httpd_req_t *req)
{
    /* URL-encoded body: ssid=VALUE&pass=VALUE (max ~100 byte) */
    char buf[256] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        ESP_LOGE(TAG, "submit_post: recv failed (len=%d)", len);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    ESP_LOGI(TAG, "POST body: %s", buf);

    /* Minimal URL-decode: + → space, %XX → char. Chars allowed: alnum + space.
     * LibreSSL kütüphanesini kullanmak yerine basit inline decoder yaptık. */
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char pass[WIFI_PASS_MAX_LEN + 1] = {0};

    static const char *ssid_key = "ssid=";
    static const char *pass_key = "pass=";

    char *p_ssid_start = strstr(buf, ssid_key);
    if (p_ssid_start) {
        p_ssid_start += strlen(ssid_key);
        char *p_ssid_end = strchr(p_ssid_start, '&');
        size_t slen = p_ssid_end ? (size_t)(p_ssid_end - p_ssid_start)
                                  : strlen(p_ssid_start);
        if (slen >= sizeof(ssid)) slen = sizeof(ssid) - 1;
        for (size_t i = 0; i < slen; i++) {
            char c = p_ssid_start[i];
            if (c == '+') c = ' ';
            else if (c == '%' && i + 2 < slen) {
                char hex[3] = { p_ssid_start[i+1], p_ssid_start[i+2], 0 };
                c = (char)strtol(hex, NULL, 16);
                i += 2;
            }
            ssid[i] = c;
        }
        ssid[slen] = '\0';
    }

    char *p_pass_start = strstr(buf, pass_key);
    if (p_pass_start) {
        p_pass_start += strlen(pass_key);
        size_t plen = strlen(p_pass_start);  /* pass son parametre, & olmaz */
        if (plen >= sizeof(pass)) plen = sizeof(pass) - 1;
        for (size_t i = 0; i < plen; i++) {
            char c = p_pass_start[i];
            if (c == '+') c = ' ';
            else if (c == '%' && i + 2 < plen) {
                char hex[3] = { p_pass_start[i+1], p_pass_start[i+2], 0 };
                c = (char)strtol(hex, NULL, 16);
                i += 2;
            }
            pass[i] = c;
        }
        pass[plen] = '\0';
    }

    ESP_LOGI(TAG, "Parsed: ssid='%s' pass_len=%u", ssid, (unsigned)strlen(pass));

    /* Validation: SSID en az 1 karakter, 32'den kısa olmalı */
    if (strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Empty SSID");
        httpd_resp_send(req, "SSID bos olamaz", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    /* NVS'e kaydet */
    esp_err_t err = wifi_cred_save(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_cred_save failed: %s", esp_err_to_name(err));
        httpd_resp_send(req, "Kayit basarisiz", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WiFi creds saved! Rebooting in 2 seconds...");

    /* Saved HTML döndür + restart tetikle */
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    /* 2 saniye sonra restart (HTTP response gitmesi için zaman) */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    /* asla buraya gelmez */
    return ESP_OK;
}

/* Captive portal: tüm GET'leri /'e yönlendir.
 * Telefonlar "captive.apple.com" gibi URL'ler dener — biz /'a redirect ederiz. */
static esp_err_t redirect_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" WIFI_PROV_AP_IP "/");
    return httpd_resp_send(req, NULL, 0);
}

/* HTTP server başlat */
static httpd_handle_t s_httpd = NULL;

static esp_err_t start_http_server(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 4096;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return ESP_FAIL;
    }

    /* Tüm URI'leri yakala — önce spesifik olanları register et */
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler
    });
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/submit", .method = HTTP_GET, .handler = submit_get_handler
    });
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/submit", .method = HTTP_POST, .handler = submit_post_handler
    });
    /* Captive portal detection — iOS/Android otomatik browser açar.
     * iOS: /hotspot-detect.html bekler, status 200 olursa "kontrol başarılı"
     *   (redirect dönerse captive var → browser açılır)
     * Android 9+: /generate_204 bekler, status 204 = "captive yok",
     *   başka şey dönerse captive var → browser açılır */
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/generate_204", .method = HTTP_GET,
        .handler = redirect_get_handler
    });
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/hotspot-detect.html", .method = HTTP_GET,
        .handler = redirect_get_handler
    });
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/connectivitycheck.html", .method = HTTP_GET,
        .handler = redirect_get_handler
    });
    httpd_register_uri_handler(s_httpd, &(httpd_uri_t){
        .uri = "/success.txt", .method = HTTP_GET,  /* BlackBerry fallback */
        .handler = redirect_get_handler
    });

    ESP_LOGI(TAG, "HTTP server started (port 80)");
    return ESP_OK;
}

/* DNS server: tüm *.x sorgularını 192.168.4.1'e yönlendir.
 * iOS/Android captive portal detection için kritik. */
static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started (port 53, all -> " WIFI_PROV_AP_IP ")");

    uint8_t buf[256];
    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);

        /* Tek recvfrom ile hem query'yi hem client addr'i al.
         * Eski kodda iki recv arka arkaya çağrılıyordu → ikincisi ilk query'yi
         * siliyordu, response asla telefona ulaşmıyordu (captive portal
         * detection için DNS A record gerekli). */
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &clen);
        if (n < 12) continue;  /* too short, skip */

        /* DNS response oluştur:
         * - Transaction ID ilk 2 byte'tan kopyalanır (doğru ID'ye response)
         * - Flags: 0x8180 = Response + No error
         * - Questions: aynen (header byte 4-5)
         * - Answer RRs: 1 ekliyoruz, Authority/Additional: 0 */
        memcpy(buf + 2, "\x81\x80", 2);
        /* Questions byte'i (4-5) zaten query'de var; onu koruyoruz.
         * Yalnızca Answer/Authority/Additional alanlarını (6-9) set et. */
        buf[6] = 0; buf[7] = 0;  /* Authority RRs = 0 */
        buf[8] = 0; buf[9] = 0;  /* Additional RRs = 0 */

        /* Question Count zaten 1 idi (QCOUNT byte 4-5). Answer RRs'i 1 yapmak için
         * byte 6-7'yi set ettik (0x00 0x01). Yine de mevcut query'de bu
         * byte'lar ANCOUNT sayısıdır; bunları 1 yapmamız lazım. */
        buf[6] = 0; buf[7] = 1;  /* ANCOUNT = 1 */

        /* Answer RR: name pointer (0xC00C = "this name, off 12" → query'den al)
         * + type A (1) + class IN (1) + TTL 60s + RDLEN 4 + RDATA 192.168.4.1 */
        size_t resp_len = (size_t)n;
        if (resp_len + 16 > sizeof(buf)) continue;  /* query çok uzun, skip */

        buf[resp_len++] = 0xC0; buf[resp_len++] = 0x0C;  /* name pointer */
        buf[resp_len++] = 0x00; buf[resp_len++] = 0x01;  /* Type A */
        buf[resp_len++] = 0x00; buf[resp_len++] = 0x01;  /* Class IN */
        buf[resp_len++] = 0x00; buf[resp_len++] = 0x00;  /* TTL = 60 */
        buf[resp_len++] = 0x00; buf[resp_len++] = 0x3C;
        buf[resp_len++] = 0x00; buf[resp_len++] = 0x04;  /* RDLENGTH = 4 */
        buf[resp_len++] = 192; buf[resp_len++] = 168;  /* 192.168 */
        buf[resp_len++] = 4;   buf[resp_len++] = 1;    /* .4.1 */

        sendto(sock, buf, resp_len, 0,
               (struct sockaddr *)&client, clen);

        ESP_LOGD(TAG, "DNS reply sent: %u bytes to %s", (unsigned)resp_len,
                 inet_ntoa(client.sin_addr));
    }

    close(sock);
    vTaskDelete(NULL);
}

/* SoftAP başlat (STA modunu da aktif tut — sonradan geçiş için) */
static esp_netif_t *s_ap_netif = NULL;

static esp_err_t start_softap(void)
{
    /* AP netif oluştur */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "ap netif create failed");
        return ESP_FAIL;
    }

    /* IP ayarla */
    esp_netif_ip_info_t ip = {
        .ip = { .addr = IPADDR4_INIT_BYTES(192,168,4,1) },
        .gw = { .addr = IPADDR4_INIT_BYTES(192,168,4,1) },
        .netmask = { .addr = IPADDR4_INIT_BYTES(255,255,255,0) },
    };
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));

    /* WiFi mode: AP+STA (STA daha sonra bağlanır) */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_PROV_AP_SSID,
            .ssid_len = strlen(WIFI_PROV_AP_SSID),
            .channel = WIFI_PROV_CHANNEL,
            .max_connection = WIFI_PROV_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = { .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    ESP_LOGI(TAG, "SoftAP started: SSID='%s' IP=" WIFI_PROV_AP_IP,
             WIFI_PROV_AP_SSID);
    return ESP_OK;
}

esp_err_t wifi_provisioning_start(void)
{
    if (s_active) {
        ESP_LOGW(TAG, "Already in provisioning mode");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "=== Provisioning MODE ===");
    ESP_LOGW(TAG, "Connect to WiFi '%s' (open), then open http://" WIFI_PROV_AP_IP,
             WIFI_PROV_AP_SSID);

    /* ESP-IDF ağ altyapısı zaten main'de başlatılmış (esp_netif_init,
     * esp_event_loop_create_default, esp_wifi_init). Burada sadece AP tarafını aç. */
    esp_err_t err = start_softap();
    if (err != ESP_OK) return err;

    /* DNS server başlat */
    xTaskCreate(dns_server_task, "dns_prov", 4096, NULL, 4, NULL);

    /* HTTP server */
    err = start_http_server();
    if (err != ESP_OK) return err;

    s_active = true;
    return ESP_OK;
}

void wifi_provisioning_stop(void)
{
    if (!s_active) return;

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    s_active = false;
    ESP_LOGI(TAG, "Provisioning stopped");
}

bool wifi_provisioning_is_active(void)
{
    return s_active;
}
