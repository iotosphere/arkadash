#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t encoder_init(void);
int32_t encoder_get_count(void);
int32_t encoder_get_diff(void);
void encoder_reset_count(void);
bool encoder_button_pressed(void);
bool key0_pressed(void);
bool key0_pressed(void);

#ifdef __cplusplus
}
#endif
