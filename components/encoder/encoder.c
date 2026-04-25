#include "encoder.h"
#include "app_config.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>

static const char *TAG = "encoder";
static pcnt_unit_handle_t s_pcnt_unit = NULL;
static int s_last_count = 0;

esp_err_t encoder_init(void)
{
    ESP_LOGI(TAG, "GPIO init - A:%d B:%d PUSH:%d KEY0:%d", ENCODER_A, ENCODER_B, ENCODER_PUSH, KEY0_PIN);

    const gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << ENCODER_A) |
                        (1ULL << ENCODER_B) |
                        (1ULL << ENCODER_PUSH) |
                        (1ULL << KEY0_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&enc_cfg));

    const pcnt_unit_config_t unit_config = {
        .high_limit = 1000,
        .low_limit  = -1000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &s_pcnt_unit));

    const pcnt_chan_config_t chan_config = {
        .edge_gpio_num  = ENCODER_A,
        .level_gpio_num = ENCODER_B,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_config, &pcnt_chan));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan,
        PCNT_CHANNEL_LEVEL_ACTION_HOLD,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

    ESP_LOGI(TAG, "Ready");
    return ESP_OK;
}

int32_t encoder_get_count(void)
{
    int count = 0;
    if (s_pcnt_unit) {
        pcnt_unit_get_count(s_pcnt_unit, &count);
    }
    return (int32_t)count;
}

int32_t encoder_get_diff(void)
{
    int32_t current = encoder_get_count();
    int32_t diff = current - s_last_count;
    s_last_count = current;
    return diff;
}

void encoder_reset_count(void)
{
    if (s_pcnt_unit) {
        pcnt_unit_clear_count(s_pcnt_unit);
    }
    s_last_count = 0;
}

bool encoder_button_pressed(void)
{
    return (gpio_get_level(ENCODER_PUSH) == 0);
}

bool key0_pressed(void)
{
    return (gpio_get_level(KEY0_PIN) == 0);
}
