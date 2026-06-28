idf_component_register(
    SRCS "websocket_client.c"
    INCLUDE_DIRS "include" "../ui"
    REQUIRES esp_websocket_client esp_event freertos esp_ringbuf
    PRIV_REQUIRES ui
)
