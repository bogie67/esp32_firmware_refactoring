idf_component_register(
    SRCS
        "transport_mqtt.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt log esp_event esp_netif
)