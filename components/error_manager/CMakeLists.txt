idf_component_register(SRCS "error_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_timer log)