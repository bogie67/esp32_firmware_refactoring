#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cmd_frame.h"

typedef struct {
    uint16_t id;
    int8_t   status;     /* 0=ok, <0=err code               */
    uint8_t *payload;    /* può essere NULL                 */
    size_t   len;        /* len del payload                 */
    origin_t origin;     /* per sapere a chi rilanciare     */
    bool     is_final;   /* ultimo pacchetto di uno stream  */
} resp_frame_t;
