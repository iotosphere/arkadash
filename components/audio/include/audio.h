#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2S and ES8311 codec
 */
esp_err_t audio_init(void);

/**
 * @brief Start recording audio from microphone
 * @param buffer Output buffer for PCM data
 * @param max_len Maximum bytes to record
 * @param bytes_read Output: actual bytes recorded
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t audio_record(uint8_t *buffer, size_t max_len, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Play PCM audio through speaker
 * @param buffer PCM data buffer (16-bit mono, 16kHz)
 * @param len Length of buffer in bytes
 * @return ESP_OK on success
 */
esp_err_t audio_play(const uint8_t *buffer, size_t len);

/**
 * @brief Stop current playback
 */
void audio_stop_playback(void);

/**
 * @brief Set speaker volume (0-100)
 */
esp_err_t audio_set_volume(uint8_t volume);

/**
 * @brief Set microphone gain (0-100)
 */
esp_err_t audio_set_mic_gain(uint8_t gain);

/**
 * @brief Start continuous recording to internal buffer
 * @return ESP_OK on success
 */
esp_err_t audio_start_continuous_record(void);

/**
 * @brief Stop recording and get the recorded data
 * @param buffer Output buffer for recorded data (caller must free)
 * @param bytes_written Output: actual bytes recorded
 * @return ESP_OK on success, ESP_FAIL if no data recorded
 */
esp_err_t audio_stop_record(uint8_t **buffer, size_t *bytes_written);

/**
 * @brief Cancel recording without saving
 */
void audio_cancel_record(void);

/**
 * @brief Check if currently recording
 */
bool audio_is_recording(void);

/**
 * @brief Get TX channel handle
 */
void *audio_get_tx_handle(void);

/**
 * @brief Get RX channel handle
 */
void *audio_get_rx_handle(void);

#ifdef __cplusplus
}
#endif
