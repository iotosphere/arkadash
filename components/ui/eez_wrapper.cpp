#include <stdint.h>
#include "vars.h"
#include "eez-flow.h"
#include "screens.h"
#include "images.h"
#include <stdio.h>

using namespace eez;

static const lv_img_dsc_t* weather_code_to_icon(int code)
{
    if (code == 0)                    return &img_sun;
    if (code >= 1 && code <= 3)       return &img_cloudy;
    if (code >= 45 && code <= 48)     return &img_foog;
    if (code >= 51 && code <= 57)     return &img_rainy;
    if (code >= 61 && code <= 67)     return &img_rainy__1_;
    if (code >= 71 && code <= 77)     return &img_snowy;
    if (code >= 80 && code <= 82)     return &img_rainy;
    if (code >= 85 && code <= 86)     return &img_snowy;
    if (code >= 95)                   return &img_storm;
    return &img_sun;
}

extern "C" {

void ui_set_time(int hour, int minute, int second, int date, int month, int year, const char *day_name)
{
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_HOUR, Value(hour));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_MINUTE, Value(minute));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_SECOND, Value(second));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_DATE, Value(date));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_MONTH, Value(month));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_YEAR, Value(year));
    if (day_name) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_DAY_NAME, Value(day_name));
    }
}

void ui_set_weather(const char *temp, const char *condition, const char *humidity)
{
    printf("ui_set_weather: temp=%s hum=%s cond=%s\n",
           temp ? temp : "(null)",
           humidity ? humidity : "(null)",
           condition ? condition : "(null)");
    printf("  objects.weather_temp_label=%p\n", (void*)objects.weather_temp_label);
    printf("  objects.weather_hum_label=%p\n", (void*)objects.weather_hum_label);
    printf("  objects.weather_state=%p\n", (void*)objects.weather_state);
    printf("  objects.weather_icon=%p\n", (void*)objects.weather_icon);

    if (temp) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_TEMP, Value(temp));
    }
    if (condition) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_CONDITION, Value(condition));
    }
    if (humidity) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_HUM, Value(humidity));
    }

    if (objects.weather_temp_label && temp) {
        lv_label_set_text(objects.weather_temp_label, temp);
        printf("  -> Set temp label OK\n");
    }
    if (objects.weather_hum_label && humidity) {
        lv_label_set_text(objects.weather_hum_label, humidity);
        printf("  -> Set hum label OK\n");
    }
    if (objects.weather_state && condition) {
        lv_label_set_text(objects.weather_state, condition);
        printf("  -> Set state label OK\n");
    }
}

void ui_set_weather_icon(int weather_code)
{
    printf("ui_set_weather_icon: code=%d, weather_icon=%p\n", weather_code, (void*)objects.weather_icon);
    if (objects.weather_icon) {
        const lv_img_dsc_t *icon = weather_code_to_icon(weather_code);
        lv_image_set_src(objects.weather_icon, icon);
        printf("  -> Set icon OK\n");
    }
}

}
