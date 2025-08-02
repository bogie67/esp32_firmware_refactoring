
idf_component_register(
    SRCS "app_main.c"
         "cmd_proc_task.c"
         "schedule.c"
         "transport_mqtt.c"
         "wifi.c"
         "solenoid.c"
         "test_wifi_simple.c"
    INCLUDE_DIRS "."
    REQUIRES transport_ble esp_wifi json freertos nvs_flash unity driver
)
