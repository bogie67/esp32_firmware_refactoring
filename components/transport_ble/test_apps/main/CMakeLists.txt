idf_component_register(SRCS "test_transport_ble_main.c" "test_transport_ble_basic.c"
                       PRIV_REQUIRES transport_ble unity
                       WHOLE_ARCHIVE)