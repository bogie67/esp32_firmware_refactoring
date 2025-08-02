
#pragma once
#include <stdint.h>
#include <stddef.h>

int8_t svc_wifi_scan(uint8_t **json_out, size_t *len);
int8_t svc_wifi_configure(const uint8_t *json_in, size_t len);
