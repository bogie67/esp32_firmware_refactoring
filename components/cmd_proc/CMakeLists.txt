idf_component_register(
    SRCS
        "cmd_proc_task.c"
    INCLUDE_DIRS "."
    REQUIRES freertos log protocol_types schedule wifi solenoid error_manager
)