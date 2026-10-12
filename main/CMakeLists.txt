idf_component_register(
    SRCS
        "main.cpp"
        "port_console.cpp"
        "port_events.cpp"
        "port_storage.cpp"
        "port_system.cpp"
        "port_wifi.cpp"
        "port_ws.cpp"
    INCLUDE_DIRS "."
    REQUIRES hermes_gadget_core
    PRIV_REQUIRES
        console
        esp_app_format
        esp_event
        esp_netif
        esp_pm
        esp_timer
        esp_wifi
        mbedtls
        nvs_flash
        esp_websocket_client
)
