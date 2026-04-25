idf_component_register(SRCS "wifi_manager.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES wifi_station esp_wifi esp_event nvs_flash freertos)
