#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_TIME_HOUR = 0,
    FLOW_GLOBAL_VARIABLE_TIME_MINUTE = 1,
    FLOW_GLOBAL_VARIABLE_TIME_DATE = 2,
    FLOW_GLOBAL_VARIABLE_TIME_MONTH = 3,
    FLOW_GLOBAL_VARIABLE_TIME_YEAR = 4,
    FLOW_GLOBAL_VARIABLE_TIME_DAY_NAME = 5,
    FLOW_GLOBAL_VARIABLE_WEATHER_TEMP = 6,
    FLOW_GLOBAL_VARIABLE_WEATHER_CONDITION = 7,
    FLOW_GLOBAL_VARIABLE_WEATHER_HUM = 8,
    FLOW_GLOBAL_VARIABLE_TIME_SECOND = 9
};

// Native global variables

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/