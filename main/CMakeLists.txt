idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash wifi_station wifi_provisioning display encoder ui clock_weather app_config lvgl esp_lvgl_port freertos games audio websocket_client discovery)
