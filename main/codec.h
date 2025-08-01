#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cmd_frame.h"
#include "resp_frame.h"

/* Decodifica frame RX → cmd_frame_t  (alloca payload se presente) */
bool     decode_ble_frame(const uint8_t *data, size_t len, cmd_frame_t *out);

/* Codifica response → buffer mallocato (ritorna len in outLen) */
uint8_t *encode_ble_resp(const resp_frame_t *r, size_t *outLen);
