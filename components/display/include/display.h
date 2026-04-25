#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
esp_lcd_panel_io_handle_t display_get_io_handle(void);
esp_lcd_panel_handle_t display_get_panel_handle(void);
void display_backlight_off(void);
void display_backlight_on(void);

#ifdef __cplusplus
}
#endif
