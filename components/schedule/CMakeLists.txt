idf_component_register(
    SRCS
        "schedule.c"
    INCLUDE_DIRS "."
    REQUIRES json log nvs_flash
)