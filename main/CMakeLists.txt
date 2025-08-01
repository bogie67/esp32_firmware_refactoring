
idf_component_register(
    SRCS  "app_main.c"
          "codec.c"
          "transport_ble.c"
          "cmd_proc_task.c"
          "schedule.c"
    REQUIRES bt json 
    PRIV_REQUIRES nvs_flash
)
