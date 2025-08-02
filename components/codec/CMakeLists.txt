idf_component_register(
    SRCS
        "codec.c"
    INCLUDE_DIRS "."
    REQUIRES esp_common log protocol_types
    PRIV_REQUIRES json
)