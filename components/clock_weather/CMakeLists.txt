idf_component_register(SRCS "clock_weather.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES ui esp_http_client esp_lvgl_port mbedtls lwip nvs_flash freertos)
