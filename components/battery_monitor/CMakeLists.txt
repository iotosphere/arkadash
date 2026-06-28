idf_component_register(
    SRCS "battery_monitor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES "driver esp_adc"
    REQUIRES "app_config"
)
