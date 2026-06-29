#include "websocket_client.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "ai_hal9000.h"

static const char *TAG = "ws_client";

static esp_websocket_client_handle_t s_ws_handle = NULL;
static bool s_connected = false;

static bool   s_waiting_for_response = false;
static bool   s_tts_complete         = false;
static bool   s_has_error            = false;  /* server error aldık, ring buffer invalid */
static size_t s_expected_size        = 0;
static bool   s_agent_mode           = false;  /* Chat=false (MiniMax), Agent=true (ZeroClaw) */

/* 64 KB statik ring buffer - TTS PCM verisi için */
static uint8_t           s_rb_storage[65536];
static StaticRingbuffer_t s_rb_struct;
static RingbufHandle_t    s_rb_handle = NULL;

/* ------------------------------------------------------------------ */

void ws_set_waiting(bool waiting)      { s_waiting_for_response = waiting; }
void ws_set_tts_complete(bool complete){ s_tts_complete = complete; }
bool ws_is_tts_complete(void)          { return s_tts_complete; }
bool ws_has_error(void)                { return s_has_error; }
uint8_t *ws_get_response_buf(void)     { return NULL; }
size_t   ws_get_response_len(void)     { return 0; }
size_t   ws_get_expected_size(void)    { return s_expected_size; }
bool     ws_is_connected(void)         { return s_connected; }

/* ------------------------------------------------------------------ */

static void drain_ring_buffer(void)
{
    if (!s_rb_handle) return;
    size_t   len  = 0;
    uint8_t *item = NULL;
    while ((item = (uint8_t *)xRingbufferReceiveUpTo(
                s_rb_handle, &len, 0, 1)) != NULL) {
        vRingbufferReturnItem(s_rb_handle, (void *)item);
    }
}

void ws_clear_response(void)
{
    s_expected_size = 0;
    s_tts_complete  = false;
    s_has_error     = false;
    drain_ring_buffer();
}

void ws_stream_clear(void) { drain_ring_buffer(); }

size_t ws_stream_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!s_rb_handle || !buf || len == 0) return 0;

    /* Error state'da ring buffer invalid — boşalt ve dön */
    if (s_has_error) {
        drain_ring_buffer();
        return 0;
    }

    size_t   item_size = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
        s_rb_handle, &item_size, pdMS_TO_TICKS(timeout_ms), len);
    if (item) {
        size_t to_copy = (item_size < len) ? item_size : len;
        memcpy(buf, item, to_copy);
        vRingbufferReturnItem(s_rb_handle, (void *)item);
        return to_copy;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static void websocket_event_handler(void *handler_args,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)handler_args;
    (void)event_base;

    esp_websocket_event_data_t *event =
        (esp_websocket_event_data_t *)event_data;

    switch (event_id) {

    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        s_connected = true;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        s_connected = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WebSocket data: len=%d fin=%d waiting=%d",
                 (int)event->data_len, event->fin, s_waiting_for_response);

        if (event->data_len <= 0) break;

        if (event->data_ptr[0] == '{') {
            /* JSON mesaj */
            char *json_buf = malloc(event->data_len + 1);
            if (!json_buf) break;
            memcpy(json_buf, event->data_ptr, event->data_len);
            json_buf[event->data_len] = '\0';

            if (strstr(json_buf, "tts_start")) {
                ESP_LOGI(TAG, "tts_start - preparing for audio");
                ws_clear_response();
                s_waiting_for_response = true;
                s_tts_complete         = false;
                /* HAL 9000 göz animasyonu: AI konuşmaya başladı */
                ai_hal9000_start();
            }
            else if (strstr(json_buf, "tts_end")) {
                s_expected_size = 0;
                char *size_ptr = strstr(json_buf, "\"size\":");
                if (size_ptr) {
                    s_expected_size = (size_t)atoi(size_ptr + 7);
                    ESP_LOGI(TAG, "tts_end - expected: %d bytes",
                             (int)s_expected_size);
                } else {
                    ESP_LOGI(TAG, "tts_end - no size info");
                }
                s_tts_complete = true;
                /* s_waiting_for_response'u burada sıfırlama —
                   binary chunk'lar hâlâ gelebilir. chat_task sıfırlar. */
                /* HAL 9000: AI konuşması bitti (binary chunk'lar hâlâ gelebilir ama
                   tts_end server tarafında ses üretiminin bittiği anlamına gelir). */
                ai_hal9000_stop();
            }
            else if (strstr(json_buf, "done")) {
                /* Tüm veri akışı tamamlandı - ring buffer boşalana kadar bekle */
                ESP_LOGI(TAG, "done - all data sent");
                s_tts_complete = true;
                ai_hal9000_stop();  /* yedek: tts_end kaçırılırsa burada dur */
            }
else if (strstr(json_buf, "error")) {
                ESP_LOGW(TAG, "Server error - invalidating ring buffer");
                s_waiting_for_response = false;
                s_has_error = true;
                drain_ring_buffer();
                ai_hal9000_stop();  /* hata durumunda da durdur */
            }
else if (strstr(json_buf, "spotify_playlists")) {
                ESP_LOGI(TAG, "spotify_playlists received");
                extern void ui_spotify_update_playlists(const char *json);
                ui_spotify_update_playlists(json_buf);
            }
            else if (strstr(json_buf, "spotify_status")) {
                ESP_LOGI(TAG, "spotify_status received");
                extern void ui_spotify_update_status(const char *json);
                ui_spotify_update_status(json_buf);
            }
            else if (strstr(json_buf, "\"type\":\"spotify_status\"")) {
                ESP_LOGI(TAG, "spotify_status received: %s", json_buf);
                extern void ui_spotify_update_status(const char *json);
                ui_spotify_update_status(json_buf);
            }
            free(json_buf);

        } else {
            /* Binary audio → ring buffer */
            if (s_rb_handle) {
                BaseType_t sent = xRingbufferSend(
                    s_rb_handle, event->data_ptr, event->data_len, 0);
                if (sent != pdTRUE) {
                    ESP_LOGW(TAG, "Ring buffer full, dropped %d bytes",
                             (int)event->data_len);
                }
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */

esp_err_t ws_init(const char *uri)
{
    if (s_ws_handle) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Connecting to %s", uri);

    if (!s_rb_handle) {
        s_rb_handle = xRingbufferCreateStatic(
            sizeof(s_rb_storage), RINGBUF_TYPE_BYTEBUF,
            s_rb_storage, &s_rb_struct);
        if (!s_rb_handle) {
            ESP_LOGE(TAG, "Failed to create ring buffer");
            return ESP_FAIL;
        }
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri              = uri,
        .buffer_size      = 8192,   /* artırıldı - büyük veri gönderimi için */

        /* FIX: keep-alive ve timeout artırıldı */
        .keep_alive_enable   = true,
        .keep_alive_idle     = 120,  /* 120s boşta kalınca ilk ping */
        .keep_alive_interval = 15,   /* ping'ler arası 15s */
        .keep_alive_count    = 5,    /* 5 başarısız ping → kapat */

        .reconnect_timeout_ms = 5000,
        .ping_interval_sec   = 60,   /* uygulama seviyesi ping: 60s */
    };

    s_ws_handle = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_handle) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_ws_handle, WEBSOCKET_EVENT_ANY,
                                  websocket_event_handler, NULL);
    return ESP_OK;
}

/* Re-init: discovery yeni IP bulduğunda çağrılır. Eski client'ı yıkıp
 * yeniden oluşturur. Ring buffer'ı koruruz (state preservasyonu). */
esp_err_t ws_reinit(const char *uri)
{
    ESP_LOGI(TAG, "Reinit: yeni URI ile yeniden bağlanılıyor: %s", uri);

    if (s_ws_handle) {
        esp_websocket_client_stop(s_ws_handle);
        esp_websocket_client_destroy(s_ws_handle);
        s_ws_handle = NULL;
    }

    return ws_init(uri);
}

esp_err_t ws_connect(void)
{
    if (!s_ws_handle) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_websocket_client_start(s_ws_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket: %s", esp_err_to_name(err));
        return err;
    }

    int retry = 0;
    while (!s_connected && retry < 100) {
        vTaskDelay(pdMS_TO_TICKS(50));
        retry++;
    }

    if (!s_connected) {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebSocket connected successfully");
    return ESP_OK;
}

esp_err_t ws_disconnect(void)
{
    if (!s_ws_handle) return ESP_OK;
    s_connected = false;
    return esp_websocket_client_stop(s_ws_handle);
}

esp_err_t ws_deinit(void)
{
    if (s_ws_handle) {
        esp_websocket_client_destroy(s_ws_handle);
        s_ws_handle = NULL;
    }
    return ESP_OK;
}

esp_err_t ws_send_audio(const uint8_t *audio_buf, size_t audio_len)
{
    if (!s_ws_handle || !s_connected) {
        ESP_LOGE(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Sending audio: %d bytes", (int)audio_len);

    esp_websocket_client_send_text(s_ws_handle, "PTT_START", 9,
                                   pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(20));

    uint32_t net_len = htonl(audio_len);
    esp_websocket_client_send_bin(s_ws_handle, (const char *)&net_len, 4,
                                  pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(20));

    size_t chunk_size = 4096;  /* artırıldı - daha hızlı gönderim */
    size_t sent       = 0;
    while (sent < audio_len) {
        size_t len = (sent + chunk_size > audio_len)
                         ? (audio_len - sent) : chunk_size;
        int ret = esp_websocket_client_send_bin(
            s_ws_handle, (const char *)(audio_buf + sent), len,
            pdMS_TO_TICKS(5000));  /* timeout artırıldı: 5s */
        if (ret < 0) {
            ESP_LOGE(TAG, "Send failed at %d/%d", (int)sent, (int)audio_len);
            return ESP_FAIL;
        }
        sent += len;
        if (sent % 8192 == 0 || sent == audio_len) {
            ESP_LOGI(TAG, "Sent %d/%d bytes", (int)sent, (int)audio_len);
        }
        vTaskDelay(pdMS_TO_TICKS(5));  /* kısaltıldı */
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    esp_websocket_client_send_text(s_ws_handle, "PTT_STOP", 8,
                                   pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Audio send complete");
    return ESP_OK;
}

esp_err_t ws_send_text(const char *text)
{
    if (!s_ws_handle || !s_connected) {
        ESP_LOGE(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }

    int len = strlen(text);
    ESP_LOGI(TAG, "Sending text: %s", text);
    return esp_websocket_client_send_text(s_ws_handle, text, len,
                                          pdMS_TO_TICKS(500));
}

void ws_set_agent_mode(bool agent_mode)
{
    s_agent_mode = agent_mode;
    ESP_LOGI(TAG, "Agent mode: %s", agent_mode ? "ON (ZeroClaw)" : "OFF (MiniMax)");
}

bool ws_is_agent_mode(void)
{
    return s_agent_mode;
}

/* ── Spotify Control ──────────────────────────────────────────── */

esp_err_t ws_send_spotify_list(void)
{
    return ws_send_text("SPOTIFY_LIST");
}

esp_err_t ws_send_spotify_play(const char *playlist_id)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "SPOTIFY_PLAY:%s", playlist_id);
    return ws_send_text(cmd);
}

esp_err_t ws_send_spotify_status(void)
{
    return ws_send_text("SPOTIFY_STATUS");
}

static void spotify_status_timer_callback(void *arg) {
    ws_send_text("SPOTIFY_STATUS");
}

esp_err_t ws_send_spotify_status_delayed(void)
{
    static esp_timer_handle_t status_timer = NULL;
    if (!status_timer) {
        esp_timer_create_args_t args = {
            .callback = spotify_status_timer_callback,
            .arg = NULL,
            .name = "spotify_status_timer"
        };
        esp_timer_create(&args, &status_timer);
    }
    esp_timer_start_once(status_timer, 500000);
    return ESP_OK;
}

esp_err_t ws_send_spotify_pause(void)
{
    return ws_send_text("SPOTIFY_PAUSE");
}

esp_err_t ws_send_spotify_resume(void)
{
    return ws_send_text("SPOTIFY_RESUME");
}

esp_err_t ws_send_spotify_skip(void)
{
    return ws_send_text("SPOTIFY_SKIP");
}

esp_err_t ws_send_spotify_toggle(void)
{
    return ws_send_text("SPOTIFY_TOGGLE");
}

/* ── LED Control ──────────────────────────────────────────── */

esp_err_t ws_send_led_toggle(void)
{
    return ws_send_text("LED_TOGGLE");
}

esp_err_t ws_send_led_color(int color_x, int color_y)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED_COLOR:%d:%d", color_x, color_y);
    return ws_send_text(cmd);
}

esp_err_t ws_send_led_brightness(int level)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED_BRIGHTNESS:%d", level);
    return ws_send_text(cmd);
}

esp_err_t ws_send_led_status(void)
{
    return ws_send_text("LED_STATUS");
}
