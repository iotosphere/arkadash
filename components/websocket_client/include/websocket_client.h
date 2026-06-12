#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t ws_init(const char *uri);
esp_err_t ws_connect(void);
esp_err_t ws_disconnect(void);
esp_err_t ws_deinit(void);
bool ws_is_connected(void);
esp_err_t ws_send_audio(const uint8_t *audio_buf, size_t audio_len);
esp_err_t ws_send_text(const char *text);
void ws_set_agent_mode(bool agent_mode);
bool ws_is_agent_mode(void);

void ws_set_audio_buffer(uint8_t *buf, size_t len);
void ws_set_waiting(bool waiting);
void ws_set_tts_complete(bool complete);
bool ws_is_tts_complete(void);
bool ws_has_error(void);
uint8_t *ws_get_response_buf(void);
size_t ws_get_response_len(void);
void ws_clear_response(void);
size_t ws_get_expected_size(void);

size_t ws_stream_read(uint8_t *buf, size_t len, uint32_t timeout_ms);
void ws_stream_clear(void);

/* Spotify Control */
esp_err_t ws_send_spotify_list(void);
esp_err_t ws_send_spotify_play(const char *playlist_id);
esp_err_t ws_send_spotify_status(void);
esp_err_t ws_send_spotify_status_delayed(void);
esp_err_t ws_send_spotify_pause(void);
esp_err_t ws_send_spotify_resume(void);
esp_err_t ws_send_spotify_skip(void);
esp_err_t ws_send_spotify_toggle(void);
