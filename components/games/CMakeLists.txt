idf_component_register(
    SRCS "bricks_breaker.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES encoder lvgl espressif__esp_lvgl_port
)
