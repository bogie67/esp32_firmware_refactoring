#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* MQTT transport component - API identica a transport_ble */

/**
 * @brief Inizializza il transport MQTT con code di comando e risposta
 * 
 * @param cmdQueue Queue per ricevere comandi dal broker MQTT
 * @param respQueue Queue per inviare risposte al broker MQTT
 */
void transport_mqtt_init(QueueHandle_t cmdQueue, QueueHandle_t respQueue);

/**
 * @brief Avvia la connessione MQTT e subscription ai topic
 */
void transport_mqtt_start(void);

/**
 * @brief Ferma la connessione MQTT
 */
void transport_mqtt_stop(void);

/**
 * @brief Ottieni stato connessione MQTT
 * 
 * @return true se connesso, false altrimenti
 */
bool transport_mqtt_is_connected(void);

/**
 * @brief Cleanup risorse MQTT
 */
void transport_mqtt_cleanup(void);