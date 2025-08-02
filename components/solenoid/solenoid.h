#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inizializza i GPIO configurati in sdkconfig */
void solenoid_init(void);

/* Accende la valvola indicata dal payload JSON: { "ch": 1 } */
int8_t svc_solenoid_on(const uint8_t *json, size_t len);

/* Spegne la valvola indicata dal payload JSON: { "ch": 1 } */
int8_t svc_solenoid_off(const uint8_t *json, size_t len);

#ifdef __cplusplus
}
#endif
