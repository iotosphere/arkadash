#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_MAIN,
    UI_SCREEN_WIFI_SCAN,
    UI_SCREEN_WIFI_CONNECT,
    UI_SCREEN_SETTINGS,
} ui_screen_t;

esp_err_t ui_init(void);
esp_err_t ui_start(void);
void ui_tick(void);
void ui_set_encoder_indev(lv_indev_t *indev);
void ui_set_encoder_diff(int32_t diff, bool btn_pressed);
esp_err_t ui_switch_screen(ui_screen_t screen);
lv_obj_t *ui_get_active_screen(void);
void ui_update_wifi_status(const char *ssid, bool connected, const char *ip);
void ui_update_scan_results(const char **ssids, int count);
esp_err_t ui_deinit(void);

void ui_set_time(int hour, int minute, int second, int date, const char *month_str, int year, const char *day_name);
void ui_set_weather(const char *temp, const char *condition, const char *humidity);
void ui_set_weather_icon(int weather_code);
void ui_set_battery_state(int percent, bool charging);
void ui_set_footer(const char *text);

#ifdef __cplusplus
}
#endif
