idf_component_register(
    SRCS
        "transport_ble.c"
        "codec.c"
    INCLUDE_DIRS "."
    REQUIRES bt freertos nvs_flash json
)