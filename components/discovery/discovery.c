/**
 * @file discovery.c
 * @brief UDP broadcast listener for agent_server IP discovery.
 *
 * Bu task UDP 53000'de dinler, "ARKADASH:<ip>" mesajlarını parse eder.
 * agent_server.py aynı port'a broadcast yapar (her 5 saniyede).
 *
 * mDNS/Bonjour alternatifi. Avantaj: agent_server IP değişse bile (reboot sonrası)
 * yeni IP broadcast ile otomatik öğrenilir.
 */

#include "discovery.h"
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "DISC";

/* Bilinen server IP'si — başta fallback, broadcast alındıkça güncellenir */
static char s_server_ip[DISCOVERY_IP_MAX_LEN] = "192.168.1.50";
static discovery_change_cb_t s_change_cb = NULL;

void discovery_set_fallback(const char *ip) {
    if (ip && strlen(ip) < DISCOVERY_IP_MAX_LEN) {
        strncpy(s_server_ip, ip, DISCOVERY_IP_MAX_LEN - 1);
        s_server_ip[DISCOVERY_IP_MAX_LEN - 1] = '\0';
    }
}

const char* discovery_get_server_ip(void) {
    return s_server_ip;
}

void discovery_set_change_cb(discovery_change_cb_t cb) {
    s_change_cb = cb;
}

/* Internal: IP geçerli mi? (basit format kontrolü) */
static bool is_valid_ip(const char *ip) {
    if (!ip || strlen(ip) < 7) return false;       /* "0.0.0.0" minimum */
    int dots = 0;
    for (const char *p = ip; *p; p++) {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') return false;
    }
    return dots == 3;
}

/* Internal: yeni IP'yi ata, değişti ise callback çağır */
static void update_server_ip(const char *new_ip) {
    if (!is_valid_ip(new_ip)) return;
    if (strcmp(new_ip, s_server_ip) == 0) return;  /* aynıysa skip */

    char old[DISCOVERY_IP_MAX_LEN];
    strncpy(old, s_server_ip, sizeof(old));
    old[sizeof(old) - 1] = '\0';

    strncpy(s_server_ip, new_ip, DISCOVERY_IP_MAX_LEN - 1);
    s_server_ip[DISCOVERY_IP_MAX_LEN - 1] = '\0';

    ESP_LOGI(TAG, "Server IP updated: %s -> %s", old, s_server_ip);

    if (s_change_cb) {
        s_change_cb(s_server_ip);
    }
}

static void udp_discovery_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d (port %d)", errno, DISCOVERY_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Discovery dinlemede: UDP %d (broadcast'lari bekliyorum)", DISCOVERY_PORT);

    char buf[64];
    while (1) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            ESP_LOGE(TAG, "recv() error: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        buf[n] = '\0';

        /* Format: "ARKADASH:<ip>[:<port>]" */
        if (n < (int)strlen(DISCOVERY_PREFIX)) continue;
        if (strncmp(buf, DISCOVERY_PREFIX, strlen(DISCOVERY_PREFIX)) != 0) continue;

        const char *ip_str = buf + strlen(DISCOVERY_PREFIX);
        char ip_buf[DISCOVERY_IP_MAX_LEN];
        strncpy(ip_buf, ip_str, sizeof(ip_buf) - 1);
        ip_buf[sizeof(ip_buf) - 1] = '\0';

        /* Opsiyonel :port ekini ayır */
        char *colon = strchr(ip_buf, ':');
        if (colon) *colon = '\0';

        update_server_ip(ip_buf);
    }

    /* asla buraya gelmez */
    close(sock);
    vTaskDelete(NULL);
}

esp_err_t discovery_start(void) {
    BaseType_t ret = xTaskCreate(
        udp_discovery_task,
        "udp_discovery",
        4096,        /* stack — basit UDP recv, yeterli */
        NULL,
        5,           /* priority */
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void discovery_wait_for_first(uint32_t timeout_ms) {
    /* Boot sonrası ilk geçerli broadcast'i bekle.
     * Fallback IP zaten set edilmiş — bulamazsak onu kullanırız. */
    ESP_LOGI(TAG, "İlk broadcast bekleniyor (timeout %u ms, fallback=%s)",
             (unsigned)timeout_ms, s_server_ip);

    uint32_t waited = 0;
    const uint32_t step_ms = 100;
    const char *initial = "192.168.1.50";
    while (waited < timeout_ms) {
        /* IP fallback'tan farklıysa discovery çalışmış demektir */
        if (strcmp(s_server_ip, initial) != 0) {
            ESP_LOGI(TAG, "İlk broadcast %u ms'de alindi", (unsigned)waited);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    ESP_LOGW(TAG, "İlk broadcast timeout (%u ms), fallback kullanılacak: %s",
             (unsigned)timeout_ms, s_server_ip);
}
