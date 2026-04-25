#pragma once

#include "sdkconfig.h"

/* TFT Display Pins (ST7789 2.4" 320x240) - SPI2_HOST */
#define TFT_SCL                 (22)
#define TFT_SDA                 (23)
#define TFT_RES                 (27)
#define TFT_DC                  (21)
#define TFT_CS                  (20)
#define TFT_BLK                 (26)

/* EC11 Rotary Encoder Pins */
#define ENCODER_A               (4)
#define ENCODER_B               (5)
#define ENCODER_PUSH            (3)

/* KEY0 */
#define KEY0_PIN                (2)

/* ST7789 Display Configuration */
#define LCD_WIDTH                240
#define LCD_HEIGHT               320
#define LCD_HOST                 SPI2_HOST
#define LCD_BK_LIGHT_ON_LEVEL   1

/* Audio (future use) */
#define EXAMPLE_RECV_BUF_SIZE   (2400)
#define EXAMPLE_SAMPLE_RATE     (16000)
#define EXAMPLE_MCLK_MULTIPLE   (384)
#define EXAMPLE_MCLK_FREQ_HZ    (EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE)
#define EXAMPLE_VOICE_VOLUME    80
#define RECORD_BUFFER_SIZE      (16000 * 2 * 5)

/* I2S Audio */
#define I2S_NUM                 (I2S_NUM_0)
#define I2S_MCLK_IO             (13)
#define I2S_BCK_IO              (12)
#define I2S_WS_IO               (10)
#define I2S_DO_IO               (9)
#define I2S_DI_IO               (11)
#define GPIO_PA_CTRL            (53)
#define EXAMPLE_MIC_GAIN        4

/* I2C */
#define I2C_NUM                 (0)
#define I2C_SCL_IO              (8)
#define I2C_SDA_IO              (7)
