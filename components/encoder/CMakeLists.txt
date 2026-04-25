idf_component_register(SRCS "encoder.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES app_config driver esp_driver_pcnt)
