idf_component_register(
    SRCS 
        "security1_session.c"
        "handshake_ble.c"
        "handshake_mqtt.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        # Core ESP-IDF components
        esp_common
        esp_timer
        esp_system
        esp_netif
        esp_event
        log
        nvs_flash
        
        # Crypto and security
        mbedtls
        protocomm
        
        # Transport protocols
        bt
        esp_wifi
        mqtt
        
        # JSON processing
        json
        
        # Threading and synchronization
        freertos
        
        # Our custom components
        error_manager
    PRIV_REQUIRES
        # Private dependencies for implementation
        unity  # For internal testing support
)