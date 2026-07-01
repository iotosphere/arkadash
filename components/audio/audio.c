#include "audio.h"
#include "app_config.h"
#include "es8311.h"

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_check.h>
#include <driver/i2s_std.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <inttypes.h>

static const char *TAG = "audio";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static es8311_handle_t es_handle = NULL;

void *audio_get_tx_handle(void)
{
    return (void *)tx_handle;
}

void *audio_get_rx_handle(void)
{
    return (void *)rx_handle;
}

static esp_err_t i2c_master_init(void)
{
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM, &i2c_cfg), TAG, "i2c config failed");
    return i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t es8311_init_codec(void)
{
    es_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_MCLK_FREQ_HZ,
        .sample_frequency = EXAMPLE_SAMPLE_RATE,
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE, EXAMPLE_SAMPLE_RATE));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, 65, NULL));  // Medium volume
    ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));
    ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, 4));  // +24 dB — was +36 dB (clipping), tried +18 dB (too quiet)

    ESP_LOGI(TAG, "ES8311 initialized");
    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    // DMA buffer ayarları - ESP32P4 için uygun değerler
    chan_cfg.dma_desc_num = 12;    // 12 buffer (2x headroom — DMA underflow önler)
    chan_cfg.dma_frame_num = 240;  // 240 frame/buffer (16kHz mono için ideal)
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_IO,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    // 🔥 RX'i sürekli açma - sadece kayıt yaparken açılacak
    // ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));  // KALDIRILDI

    ESP_LOGI(TAG, "I2S initialized (MCLK:%d BCLK:%d WS:%d DOUT:%d DIN:%d)",
             I2S_MCLK_IO, I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO);
    ESP_LOGI(TAG, "DMA: %" PRIu32 " desc x %" PRIu32 " frame", chan_cfg.dma_desc_num, chan_cfg.dma_frame_num);
    return ESP_OK;
}

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Initializing audio...");

    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << GPIO_PA_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(GPIO_PA_CTRL, 1);

    /* Init I2S first (like Waveshare demo) */
    ESP_ERROR_CHECK(i2s_init());

    /* Then init I2C and ES8311 */
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_ERROR_CHECK(es8311_init_codec());

    ESP_LOGI(TAG, "Audio initialized successfully");
    return ESP_OK;
}

esp_err_t audio_record(uint8_t *buffer, size_t max_len, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!rx_handle || !buffer) {
        ESP_LOGE(TAG, "audio_record: invalid args rx=%p buf=%p", rx_handle, buffer);
        return ESP_ERR_INVALID_ARG;
    }

    /* Ensure RX is enabled before reading */
    esp_err_t err = i2s_channel_enable(rx_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RX enable failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total_read = 0;
    uint32_t start_time = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "audio_record: max_len=%d, timeout=%ums", (int)max_len, (int)timeout_ms);

    while (total_read < max_len) {
        size_t chunk_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, buffer + total_read,
                                          max_len - total_read, &chunk_read,
                                          pdMS_TO_TICKS(100));
        if (ret == ESP_OK && chunk_read > 0) {
            total_read += chunk_read;
            ESP_LOGD(TAG, "Read %d bytes, total=%d", (int)chunk_read, (int)total_read);
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(ret));
            break;
        }

        if ((esp_timer_get_time() / 1000 - start_time) > timeout_ms) {
            break;
        }
    }

    if (bytes_read) {
        *bytes_read = total_read;
    }
    ESP_LOGI(TAG, "audio_record done: %d bytes", (int)total_read);

    return (total_read > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_play(const uint8_t *buffer, size_t len)
{
    if (!tx_handle || !buffer || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_written = 0;
    // 🔥 DMA ile uyumlu chunk boyutu
    const size_t chunk = 1024;
    
    while (total_written < len) {
        size_t to_write = (total_written + chunk > len) ? (len - total_written) : chunk;
        size_t chunk_written = 0;
        
        // 🔥 Timeout düşürüldü - 500ms yerine 20ms (kesik sesi önler)
        esp_err_t ret = i2s_channel_write(tx_handle, buffer + total_written, to_write, 
                                          &chunk_written, pdMS_TO_TICKS(20));
        
        if (ret == ESP_OK && chunk_written > 0) {
            total_written += chunk_written;
        } else {
            ESP_LOGW(TAG, "Write error at %d/%d", (int)total_written, (int)len);
            break;
        }
    }
    
    ESP_LOGI(TAG, "Playback complete, wrote %d bytes", (int)total_written);
    return ESP_OK;
}

void audio_stop_playback(void)
{
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
    }
}

esp_err_t audio_set_volume(uint8_t volume)
{
    if (!es_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (volume > 100) {
        volume = 100;
    }
    return es8311_voice_volume_set(es_handle, volume, NULL);
}

esp_err_t audio_set_mic_gain(uint8_t gain)
{
    if (!es_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (gain > 100) {
        gain = 100;
    }
    return es8311_microphone_gain_set(es_handle, gain);
}

esp_err_t audio_start_continuous_record(void)
{
    if (!rx_handle) {
        ESP_LOGE(TAG, "I2S RX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset RX channel */
    i2s_channel_disable(rx_handle);
    i2s_channel_enable(rx_handle);

    ESP_LOGI(TAG, "Recording ready");
    return ESP_OK;
}

esp_err_t audio_stop_record(uint8_t **buffer, size_t *bytes_written)
{
    if (!bytes_written) {
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_written = 0;
    return ESP_OK;
}

void audio_cancel_record(void)
{
    ESP_LOGI(TAG, "Recording cancelled");
}

bool audio_is_recording(void)
{
    return false;
}

void audio_record_continuous_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelete(NULL);
}