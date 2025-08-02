idf_component_register(
    SRCS
        "transport_ble.c"
    INCLUDE_DIRS "."
    REQUIRES bt freertos nvs_flash json codec protocol_types chunk_manager error_manager security1_session
    PRIV_REQUIRES main
)