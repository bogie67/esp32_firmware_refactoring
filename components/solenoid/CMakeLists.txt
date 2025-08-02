idf_component_register(
    SRCS
        "solenoid.c"
    INCLUDE_DIRS "."
    REQUIRES driver json log
)