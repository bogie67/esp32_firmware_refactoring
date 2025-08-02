
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cmd_frame.h"

typedef struct {
    uint16_t id;
    int8_t   status;     // 0 = ok, <0 = error code
    uint8_t* payload;    // may be NULL
    size_t   len;        // payload length
    origin_t origin;
    bool     is_final;   // last frame of stream
} resp_frame_t;
