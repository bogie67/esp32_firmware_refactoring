idf_component_register(
    SRCS
        "transport_ble.c"
    INCLUDE_DIRS "."
    REQUIRES bt freertos nvs_flash json codec protocol_types
    PRIV_REQUIRES main
)