
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    ORIGIN_BLE,
    ORIGIN_MQTT,
    ORIGIN_USB
} origin_t;

typedef struct {
    uint16_t id;
    char     op[16];   // zero-terminated command name
    uint8_t* payload;  // points to heap buffer (JSON) - free when done
    size_t   len;
    origin_t origin;
} cmd_frame_t;
