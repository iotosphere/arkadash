#include "display.h"
#include "app_config.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <lvgl.h>
#include <esp_lvgl_port.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;

static void init_lcd_spi(void)
{
    ESP_LOGI(TAG, "SPI bus init - CLK:%d MOSI:%d", TFT_SCL, TFT_SDA);

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num     = TFT_SCL,
        .mosi_io_num     = TFT_SDA,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,  // Smaller buffer to prevent SPI queue overflow
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
}

static void init_lcd_panel(void)
{
    ESP_LOGI(TAG, "ST7789 panel init - CS:%d DC:%d RST:%d", TFT_CS, TFT_DC, TFT_RES);

    // Let esp_lvgl_port handle the color trans done callback internally
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num         = TFT_DC,
        .cs_gpio_num         = TFT_CS,
        .pclk_hz             = 20 * 1000 * 1000,  // 20MHz - valid for ESP32-P4
        .spi_mode            = 0,
        .trans_queue_depth   = 3,  // Reduced from 10 to prevent queue backup
        .on_color_trans_done = NULL,  // esp_lvgl_port manages this
        .user_ctx            = NULL,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &s_io_handle));

    const esp_lcd_panel_dev_config_t panel_dev_config = {
        .reset_gpio_num = TFT_RES,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_dev_config, &s_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    ESP_LOGI(TAG, "ST7789 ready");
}

static void init_backlight(void)
{
    ESP_LOGI(TAG, "Backlight GPIO:%d", TFT_BLK);

    const gpio_config_t bk_cfg = {
        .pin_bit_mask = (1ULL << TFT_BLK),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(TFT_BLK, LCD_BK_LIGHT_ON_LEVEL);
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "========== Display init ==========");

    init_lcd_spi();
    init_lcd_panel();
    init_backlight();

    ESP_LOGI(TAG, "========== Display ready ==========");
    return ESP_OK;
}

esp_lcd_panel_io_handle_t display_get_io_handle(void)
{
    return s_io_handle;
}

esp_lcd_panel_handle_t display_get_panel_handle(void)
{
    return s_panel_handle;
}

void display_backlight_off(void)
{
    gpio_set_level(TFT_BLK, !LCD_BK_LIGHT_ON_LEVEL);
    ESP_LOGI(TAG, "Backlight OFF");
}

void display_backlight_on(void)
{
    gpio_set_level(TFT_BLK, LCD_BK_LIGHT_ON_LEVEL);
    ESP_LOGI(TAG, "Backlight ON");
}

void display_backlight_set(uint8_t pct)
{
    if (pct > 100) pct = 100;
    /* LEDC duty resolution is 8-bit (configured in display_init's
     * ledc_timer_config), so map 0..100% -> 0..255. */
    uint32_t duty = (255 * (uint32_t)pct) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
