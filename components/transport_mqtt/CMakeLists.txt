idf_component_register(
    SRCS "transport_mqtt.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt json freertos log esp_event esp_netif codec protocol_types
)