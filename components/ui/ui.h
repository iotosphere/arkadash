#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>

#include "eez-flow.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t assets[8804];

void ui_init();
void ui_tick();

void ui_set_time(int hour, int minute, int second, int date, int month, int year, const char *day_name);
void ui_set_weather(const char *temp, const char *condition, const char *humidity);
void ui_set_weather_icon(int weather_code);

void ui_set_time(int hour, int minute, int second, int date, int month, int year, const char *day_name);
void ui_set_weather(const char *temp, const char *condition, const char *humidity);
void ui_set_weather_icon(int weather_code);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H