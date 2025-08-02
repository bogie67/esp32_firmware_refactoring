#pragma once

#include <stdint.h>
#include <stddef.h>

/* MQTT transport component per comunicazione cloud */

/* Inizializza il client MQTT */
int transport_mqtt_init(void);

/* Connette al broker MQTT */
int transport_mqtt_connect(const char *broker_url);

/* Disconnette dal broker MQTT */
void transport_mqtt_disconnect(void);

/* Pubblica un messaggio su un topic */
int transport_mqtt_publish(const char *topic, const uint8_t *data, size_t len);

/* Subscribe a un topic */
int transport_mqtt_subscribe(const char *topic);

/* Cleanup risorse MQTT */
void transport_mqtt_cleanup(void);