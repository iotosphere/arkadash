#include "websocket_client.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

static const char *TAG = "ws_client";

static esp_websocket_client_handle_t s_ws_handle = NULL;
static bool s_connected = false;

// Audio buffer and callback
static bool s_waiting_for_response = false;
static bool s_tts_complete = false;
static size_t s_expected_size = 0;  // Expected total PCM bytes from tts_end

// Streaming ring buffer (64KB static - no heap allocation needed)
static uint8_t s_rb_storage[65536];
static StaticRingbuffer_t s_rb_struct;
static RingbufHandle_t s_rb_handle = NULL;

void ws_set_waiting(bool waiting)
{
    s_waiting_for_response = waiting;
}

void ws_set_tts_complete(bool complete)
{
    s_tts_complete = complete;
}

bool ws_is_tts_complete(void)
{
    return s_tts_complete;
}

uint8_t *ws_get_response_buf(void)
{
    return NULL;  // Legacy, streaming mode uses ws_stream_read()
}

size_t ws_get_response_len(void)
{
    return 0;  // Legacy, not used with streaming
}

size_t ws_get_expected_size(void)
{
    return s_expected_size;
}

void ws_clear_response(void)
{
    s_expected_size = 0;
    s_tts_complete = false;
    if (s_rb_handle) {
        // Drain ring buffer (xRingbufferReset not available in ESP-IDF v5.4)
        size_t len = 0;
        uint8_t *item;
        while ((item = (uint8_t *)xRingbufferReceiveUpTo(s_rb_handle, &len, 0, 1)) != NULL) {
            vRingbufferReturnItem(s_rb_handle, (void *)item);
        }
    }
}

size_t ws_stream_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (!s_rb_handle || !buf || len == 0) return 0;
    size_t item_size = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_rb_handle, &item_size, pdMS_TO_TICKS(timeout_ms), len);
    if (item) {
        size_t to_copy = (item_size < len) ? item_size : len;
        memcpy(buf, item, to_copy);
        vRingbufferReturnItem(s_rb_handle, (void *)item);
        return to_copy;
    }
    return 0;
}

void ws_stream_clear(void)
{
    if (s_rb_handle) {
        size_t len = 0;
        uint8_t *item;
        while ((item = (uint8_t *)xRingbufferReceiveUpTo(s_rb_handle, &len, 0, 1)) != NULL) {
            vRingbufferReturnItem(s_rb_handle, (void *)item);
        }
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)event_base;
    
    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)event_data;
    
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
        
        // Check if this is JSON (starts with '{') or binary audio
        if (event->data_len > 0 && event->data_ptr[0] == '{') {
            // JSON text message
            char *json_buf = malloc(event->data_len + 1);
            if (json_buf) {
                memcpy(json_buf, event->data_ptr, event->data_len);
                json_buf[event->data_len] = '\0';
                
                if (strstr(json_buf, "tts_start")) {
                    ESP_LOGI(TAG, "tts_start - preparing for audio");
                    ws_clear_response();
                    s_waiting_for_response = true;
                    s_tts_complete = false;  // Reset for new TTS
                }
                else if (strstr(json_buf, "tts_end")) {
                    // Parse expected size from tts_end message: {"type":"tts_end","size":305664}
                    s_expected_size = 0;
                    char *size_ptr = strstr(json_buf, "\"size\":");
                    if (size_ptr) {
                        s_expected_size = atoi(size_ptr + 7);
                        ESP_LOGI(TAG, "tts_end - expected: %d bytes", (int)s_expected_size);
                    } else {
                        ESP_LOGI(TAG, "tts_end - no size info");
                    }
                    s_tts_complete = true;  // Signal playback ready
                    // NOTE: Do NOT reset s_waiting_for_response here!
                    // Binary chunks may still be in transit. 
                    // chat_task will reset after playback completes.
                }
                else if (strstr(json_buf, "error")) {
                    ESP_LOGW(TAG, "Server error");
                    s_waiting_for_response = false;
                }
                free(json_buf);
            }
        }
        else if (event->data_len > 0) {
            // Binary audio data - always stream to ring buffer (drop if full)
            if (s_rb_handle) {
                BaseType_t sent = xRingbufferSend(s_rb_handle, event->data_ptr, event->data_len, 0);
                if (sent != pdTRUE) {
                    ESP_LOGW(TAG, "Ring buffer full, dropped %d bytes", (int)event->data_len);
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

esp_err_t ws_init(const char *uri)
{
    if (s_ws_handle) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Connecting to %s", uri);

    // Create static ring buffer for streaming audio (no heap needed)
    if (!s_rb_handle) {
        s_rb_handle = xRingbufferCreateStatic(sizeof(s_rb_storage), RINGBUF_TYPE_BYTEBUF, s_rb_storage, &s_rb_struct);
        if (!s_rb_handle) {
            ESP_LOGE(TAG, "Failed to create ring buffer");
            return ESP_FAIL;
        }
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .buffer_size = 4096,   // Small WS RX buffer - we drain to ring buffer quickly
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
        .reconnect_timeout_ms = 1000,
        .ping_interval_sec = 10,
    };

    s_ws_handle = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_handle) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }
    
    esp_websocket_register_events(s_ws_handle, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    
    return ESP_OK;
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
    
    // Wait for connection
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
    if (!s_ws_handle) {
        return ESP_OK;
    }
    
    s_connected = false;
    esp_err_t err = esp_websocket_client_stop(s_ws_handle);
    return err;
}

esp_err_t ws_deinit(void)
{
    if (s_ws_handle) {
        esp_websocket_client_destroy(s_ws_handle);
        s_ws_handle = NULL;
    }

    return ESP_OK;
}

bool ws_is_connected(void)
{
    return s_connected;
}

esp_err_t ws_send_audio(const uint8_t *audio_buf, size_t audio_len)
{
    if (!s_ws_handle || !s_connected) {
        ESP_LOGE(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Sending audio: %d bytes", (int)audio_len);
    
    // Send PTT_START to signal recording started (8 chars, no null)
    esp_websocket_client_send_text(s_ws_handle, "PTT_START", 9, pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Send audio size header (4 bytes, big-endian)
    uint32_t net_len = htonl(audio_len);
    esp_websocket_client_send_bin(s_ws_handle, (const char *)&net_len, 4, pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Send audio in chunks
    size_t chunk_size = 1024;
    size_t sent = 0;
    
    while (sent < audio_len) {
        size_t len = (sent + chunk_size > audio_len) ? (audio_len - sent) : chunk_size;
        
        int ret = esp_websocket_client_send_bin(s_ws_handle, (const char *)(audio_buf + sent), len, pdMS_TO_TICKS(1000));
        
        if (ret < 0) {
            ESP_LOGE(TAG, "Send failed at %d/%d", (int)sent, (int)audio_len);
            return ESP_FAIL;
        }
        
        sent += len;
        if (sent % 4096 == 0 || sent == audio_len) {
            ESP_LOGI(TAG, "Sent %d/%d bytes", (int)sent, (int)audio_len);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Send PTT_STOP to signal recording ended
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_websocket_client_send_text(s_ws_handle, "PTT_STOP", 8, pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Audio send complete");
    return ESP_OK;
}
