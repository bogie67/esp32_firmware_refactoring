
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES transport_ble codec protocol_types wifi solenoid schedule transport_mqtt cmd_proc esp_wifi json freertos nvs_flash unity driver error_manager security1_session
)
