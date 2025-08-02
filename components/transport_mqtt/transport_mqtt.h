#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* MQTT transport component - API identica a transport_ble */

/**
 * @brief Stato del transport MQTT (Enhanced per Security1)
 */
typedef enum {
    MQTT_DOWN = 0,                ///< MQTT disconnesso o in errore
    MQTT_CONNECTING,              ///< Connessione al broker in corso
    MQTT_UP,                      ///< MQTT connesso e funzionante
    
    // NEW: Security1 states
    MQTT_SECURITY1_HANDSHAKE,     ///< Security1 handshake su topic dedicati
    MQTT_SECURITY1_READY,         ///< Security1 session stabilita
    MQTT_OPERATIONAL,             ///< Topic operativi attivi (encrypted)
    MQTT_ENCRYPTED_COMM           ///< Comunicazione crittografata attiva
} mqtt_state_t;

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
 * @brief Ottieni stato connessione MQTT (legacy)
 * 
 * @return true se connesso, false altrimenti
 */
bool transport_mqtt_is_connected(void);

/**
 * @brief Ottiene lo stato del transport MQTT
 * 
 * @return mqtt_state_t Stato corrente (MQTT_UP/MQTT_DOWN)
 */
mqtt_state_t transport_mqtt_get_state(void);

/**
 * @brief Cleanup risorse MQTT
 */
void transport_mqtt_cleanup(void);

/* ──────────────── Security1 Integration API ──────────────── */

/**
 * @brief Configurazione Security1 per transport MQTT
 */
typedef struct {
    char broker_uri[256];              ///< URI broker MQTT
    char topic_prefix[64];             ///< Prefisso topic (es: "security1/device123")
    char client_id[64];                ///< Client ID univoco
    char proof_of_possession[64];      ///< PoP per handshake Security1
    uint8_t qos_level;                 ///< QoS level (0,1,2)
    uint16_t keepalive_interval;       ///< Keepalive seconds
    bool enable_encryption;            ///< Abilita crittografia sessione
    bool fallback_to_legacy;          ///< Fallback a modalità legacy se handshake fallisce
} transport_mqtt_security1_config_t;

/**
 * @brief Avvia transport MQTT con supporto Security1 (Dual Topic)
 * 
 * Inizializza sia i topic handshake che operativi:
 * - {prefix}/handshake/request + {prefix}/handshake/response (Security1)
 * - {prefix}/data/request + {prefix}/data/response (encrypted data)
 * 
 * @param cmdQueue Queue per comandi
 * @param respQueue Queue per risposte  
 * @param sec1_config Configurazione Security1
 * @return esp_err_t ESP_OK se avvio riuscito
 */
esp_err_t transport_mqtt_start_with_security1(QueueHandle_t cmdQueue, 
                                             QueueHandle_t respQueue,
                                             const transport_mqtt_security1_config_t *sec1_config);

/**
 * @brief Invia dati crittografati via MQTT
 * 
 * Utilizza la sessione Security1 per crittografare i dati prima della pubblicazione
 * 
 * @param data Dati da inviare
 * @param len Lunghezza dati
 * @return esp_err_t ESP_OK se invio riuscito
 */
esp_err_t transport_mqtt_send_encrypted(const uint8_t *data, size_t len);

/**
 * @brief Verifica se Security1 è attivo
 * 
 * @return true se sessione Security1 stabilita e operativa
 */
bool transport_mqtt_is_security1_active(void);

/**
 * @brief Ottieni informazioni sessione Security1
 * 
 * @param session_established Output: true se sessione stabilita
 * @param encryption_active Output: true se crittografia attiva
 * @param handshake_topics_active Output: true se topic handshake attivi
 * @param operational_topics_active Output: true se topic operativi attivi
 * @return esp_err_t ESP_OK se informazioni ottenute
 */
esp_err_t transport_mqtt_get_security1_info(bool *session_established,
                                           bool *encryption_active, 
                                           bool *handshake_topics_active,
                                           bool *operational_topics_active);

/**
 * @brief Forza transizione a modalità operativa
 * 
 * Switch dai topic handshake ai topic operativi
 * 
 * @return esp_err_t ESP_OK se transizione riuscita
 */
esp_err_t transport_mqtt_transition_to_operational(void);

/**
 * @brief Pubblica messaggio su topic handshake TX (per Security1)
 * 
 * Funzione utility per permettere al framework Security1 di pubblicare
 * risposte handshake sul topic appropriato.
 * 
 * @param topic Topic su cui pubblicare
 * @param data Dati da pubblicare
 * @param data_len Lunghezza dati
 * @return esp_err_t ESP_OK se pubblicazione riuscita
 */
esp_err_t transport_mqtt_publish_handshake_response(const char *topic, 
                                                   const uint8_t *data, 
                                                   size_t data_len);