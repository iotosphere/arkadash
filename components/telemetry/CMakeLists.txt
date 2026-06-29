idf_component_register(
    SRCS "telemetry.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_timer
        freertos
        discovery
        lwip
)
