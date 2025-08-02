/**
 * @file handshake_mqtt.h
 * @brief MQTT Handshake Transport per Security1 Session
 * 
 * Implementazione custom protocomm transport per MQTT che fornisce
 * handshake Security1 su topic standard. Gestisce connessione broker,
 * subscription topic e integration con protocomm core.
 */

#pragma once

#include "security1_session.h"
#include "protocomm.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== MQTT CONSTANTS ====================

#define HANDSHAKE_MQTT_DEFAULT_PORT         1883
#define HANDSHAKE_MQTT_DEFAULT_SECURE_PORT  8883
#define HANDSHAKE_MQTT_DEFAULT_QOS          1
#define HANDSHAKE_MQTT_DEFAULT_KEEPALIVE    60
#define HANDSHAKE_MQTT_MAX_PAYLOAD_SIZE     4096
#define HANDSHAKE_MQTT_CONNECT_TIMEOUT      10000  // 10 seconds

// Topic suffix standard per handshake
#define HANDSHAKE_MQTT_RX_SUFFIX           "/rx"    // Client pubblica qui
#define HANDSHAKE_MQTT_TX_SUFFIX           "/tx"    // Server pubblica qui
#define HANDSHAKE_MQTT_STATUS_SUFFIX       "/status" // Status updates

// ==================== TYPES ====================

/**
 * @brief Stati specifici handshake MQTT
 */
typedef enum {
    HANDSHAKE_MQTT_STATE_IDLE,          ///< Non inizializzato
    HANDSHAKE_MQTT_STATE_CONNECTING,    ///< Connessione al broker
    HANDSHAKE_MQTT_STATE_CONNECTED,     ///< Connesso, setup topic
    HANDSHAKE_MQTT_STATE_SUBSCRIBED,    ///< Topic sottoscritti
    HANDSHAKE_MQTT_STATE_READY,         ///< Pronto per handshake
    HANDSHAKE_MQTT_STATE_HANDSHAKING,   ///< Handshake in corso
    HANDSHAKE_MQTT_STATE_COMPLETE,      ///< Handshake completato
    HANDSHAKE_MQTT_STATE_ERROR,         ///< Errore MQTT
    HANDSHAKE_MQTT_STATE_DISCONNECTED   ///< Disconnesso dal broker
} handshake_mqtt_state_t;

/**
 * @brief Tipi di autenticazione MQTT
 */
typedef enum {
    HANDSHAKE_MQTT_AUTH_NONE,           ///< Nessuna autenticazione
    HANDSHAKE_MQTT_AUTH_USERNAME,       ///< Username + password
    HANDSHAKE_MQTT_AUTH_CERTIFICATE,    ///< Client certificate
    HANDSHAKE_MQTT_AUTH_PSK             ///< Pre-shared key
} handshake_mqtt_auth_type_t;

/**
 * @brief Configurazione autenticazione MQTT
 */
typedef struct {
    handshake_mqtt_auth_type_t auth_type;
    
    // Username/password authentication
    char username[64];
    char password[128];
    
    // Certificate authentication (paths to files)
    char client_cert_path[256];
    char client_key_path[256];
    char ca_cert_path[256];
    
    // PSK authentication
    char psk_identity[64];
    char psk_key[128];
} handshake_mqtt_auth_config_t;

/**
 * @brief Callback per eventi specifici MQTT
 * @param state Stato MQTT corrente
 * @param event_data Dati evento MQTT (esp_mqtt_event_handle_t)
 * @param user_data User data fornito durante init
 */
typedef void (*handshake_mqtt_event_callback_t)(handshake_mqtt_state_t state,
                                                void *event_data,
                                                void *user_data);

/**
 * @brief Configurazione completa handshake MQTT
 */
typedef struct {
    // Configurazione broker
    char broker_uri[SECURITY1_MAX_URI_LENGTH];      ///< URI completo broker
    uint16_t port;                                  ///< Porta broker (0=auto)
    bool use_ssl;                                   ///< Abilita SSL/TLS
    
    // Configurazione topic
    char topic_prefix[SECURITY1_MAX_TOPIC_LENGTH];  ///< Prefisso topic handshake
    uint8_t qos_level;                              ///< QoS level (0,1,2)
    bool retain_messages;                           ///< Retain flag messaggi
    
    // Configurazione client
    char client_id[SECURITY1_MAX_DEVICE_NAME];      ///< Client ID unico
    uint16_t keepalive_interval;                    ///< Keepalive seconds
    uint16_t connect_timeout_ms;                    ///< Timeout connessione
    
    // Configurazione autenticazione
    handshake_mqtt_auth_config_t auth;              ///< Credenziali accesso
    
    // Last Will Testament (opzionale)
    char lwt_topic[SECURITY1_MAX_TOPIC_LENGTH];     ///< Topic LWT
    char lwt_message[256];                          ///< Messaggio LWT
    uint8_t lwt_qos;                               ///< QoS LWT
    bool lwt_retain;                               ///< Retain LWT
    
    // Callbacks eventi MQTT (opzionale)
    handshake_mqtt_event_callback_t event_callback; ///< Callback eventi
    void *user_data;                                ///< User data callback
} handshake_mqtt_config_t;

/**
 * @brief Statistiche specifiche MQTT
 */
typedef struct {
    uint32_t connection_attempts;           ///< Tentativi connessione
    uint32_t successful_connections;        ///< Connessioni riuscite
    uint32_t disconnection_count;           ///< Numero disconnessioni
    uint32_t messages_received;             ///< Messaggi ricevuti totali
    uint32_t messages_sent;                 ///< Messaggi inviati totali
    uint32_t handshake_messages_received;   ///< Messaggi handshake ricevuti
    uint32_t handshake_messages_sent;       ///< Messaggi handshake inviati
    uint32_t last_connection_duration_ms;   ///< Durata ultima connessione
    uint32_t total_connection_time_ms;      ///< Tempo connessione totale
    int32_t last_error_code;               ///< Ultimo codice errore MQTT
} handshake_mqtt_stats_t;

/**
 * @brief Informazioni connessione MQTT corrente
 */
typedef struct {
    handshake_mqtt_state_t state;           ///< Stato corrente
    char broker_address[256];               ///< Indirizzo broker risolto
    uint16_t broker_port;                   ///< Porta broker connessa
    bool is_secure_connection;              ///< Connessione SSL attiva
    uint32_t connection_uptime_ms;          ///< Uptime connessione corrente
    char rx_topic[SECURITY1_MAX_TOPIC_LENGTH];  ///< Topic RX completo
    char tx_topic[SECURITY1_MAX_TOPIC_LENGTH];  ///< Topic TX completo
} handshake_mqtt_connection_info_t;

// ==================== CORE MQTT API ====================

/**
 * @brief Avvia handshake Security1 su MQTT
 * 
 * Connette al broker MQTT, sottoscrive topic handshake, prepara
 * endpoint protocomm per gestire messaggi Security1.
 * 
 * @param pc Istanza protocomm da utilizzare
 * @param config Configurazione MQTT specifica
 * @return ESP_OK se MQTT avviato correttamente
 */
esp_err_t handshake_mqtt_start(protocomm_t *pc, const handshake_mqtt_config_t *config);

/**
 * @brief Ferma handshake MQTT e cleanup risorse
 * 
 * Disconnette dal broker, libera client MQTT, ferma task.
 * 
 * @param pc Istanza protocomm utilizzata
 * @return ESP_OK se cleanup completato
 */
esp_err_t handshake_mqtt_stop(protocomm_t *pc);

/**
 * @brief Verifica se handshake MQTT è attivo
 * @param pc Istanza protocomm
 * @return true se MQTT connesso e topic sottoscritti
 */
bool handshake_mqtt_is_active(protocomm_t *pc);

/**
 * @brief Verifica se client è connesso al broker
 * @return true se connessione MQTT attiva
 */
bool handshake_mqtt_is_connected(void);

/**
 * @brief Ottieni stato corrente handshake MQTT
 * @return Stato MQTT corrente
 */
handshake_mqtt_state_t handshake_mqtt_get_state(void);

// ==================== MQTT MANAGEMENT API ====================

/**
 * @brief Riconnetti al broker MQTT
 * 
 * Forza riconnessione se disconnesso o in errore.
 * 
 * @return ESP_OK se riconnessione avviata
 */
esp_err_t handshake_mqtt_reconnect(void);

/**
 * @brief Disconnetti dal broker MQTT
 * 
 * Disconnessione pulita dal broker mantenendo configurazione.
 * 
 * @return ESP_OK se disconnessione avviata
 */
esp_err_t handshake_mqtt_disconnect(void);

/**
 * @brief Pubblica messaggio su topic handshake TX
 * 
 * Utility per inviare risposte handshake manualmente.
 * 
 * @param payload Dati da pubblicare
 * @param payload_len Lunghezza payload
 * @return ESP_OK se pubblicazione riuscita
 */
esp_err_t handshake_mqtt_publish_response(const uint8_t *payload, size_t payload_len);

/**
 * @brief Sottoscrivi topic aggiuntivo
 * 
 * Aggiunge subscription oltre a quelle standard handshake.
 * 
 * @param topic Topic da sottoscrivere
 * @param qos QoS level per subscription
 * @return ESP_OK se subscription riuscita
 */
esp_err_t handshake_mqtt_subscribe_topic(const char *topic, uint8_t qos);

/**
 * @brief Rimuovi subscription topic
 * 
 * @param topic Topic da cui rimuovere subscription
 * @return ESP_OK se unsubscription riuscita
 */
esp_err_t handshake_mqtt_unsubscribe_topic(const char *topic);

// ==================== CONFIGURATION API ====================

/**
 * @brief Aggiorna configurazione broker
 * 
 * Modifica broker URI durante runtime (richiede riconnessione).
 * 
 * @param broker_uri Nuovo URI broker
 * @param port Nuova porta (0 per default)
 * @param use_ssl Abilita/disabilita SSL
 * @return ESP_OK se configurazione aggiornata
 */
esp_err_t handshake_mqtt_update_broker(const char *broker_uri, 
                                      uint16_t port, 
                                      bool use_ssl);

/**
 * @brief Aggiorna credenziali autenticazione
 * 
 * @param auth_config Nuova configurazione autenticazione
 * @return ESP_OK se credenziali aggiornate
 */
esp_err_t handshake_mqtt_update_auth(const handshake_mqtt_auth_config_t *auth_config);

/**
 * @brief Aggiorna configurazione QoS
 * 
 * @param qos_level Nuovo QoS level (0,1,2)
 * @param retain_messages Nuovo flag retain
 * @return ESP_OK se QoS aggiornato
 */
esp_err_t handshake_mqtt_update_qos(uint8_t qos_level, bool retain_messages);

/**
 * @brief Configura Last Will Testament
 * 
 * @param lwt_topic Topic LWT
 * @param lwt_message Messaggio LWT
 * @param lwt_qos QoS LWT
 * @param lwt_retain Retain LWT
 * @return ESP_OK se LWT configurato
 */
esp_err_t handshake_mqtt_set_lwt(const char *lwt_topic,
                                const char *lwt_message,
                                uint8_t lwt_qos,
                                bool lwt_retain);

// ==================== DIAGNOSTICS API ====================

/**
 * @brief Ottieni statistiche MQTT correnti
 * @param stats Buffer output per statistiche
 * @return ESP_OK se statistiche ottenute
 */
esp_err_t handshake_mqtt_get_stats(handshake_mqtt_stats_t *stats);

/**
 * @brief Reset statistiche MQTT
 * 
 * Azzera contatori ma mantiene connessione attiva.
 */
void handshake_mqtt_reset_stats(void);

/**
 * @brief Ottieni informazioni connessione corrente
 * @param info Buffer output per informazioni connessione
 * @return ESP_OK se informazioni ottenute
 */
esp_err_t handshake_mqtt_get_connection_info(handshake_mqtt_connection_info_t *info);

/**
 * @brief Verifica raggiungibilità broker
 * 
 * Test connettività di rete verso broker configurato.
 * 
 * @param timeout_ms Timeout test in millisecondi
 * @return ESP_OK se broker raggiungibile
 */
esp_err_t handshake_mqtt_test_connectivity(uint32_t timeout_ms);

/**
 * @brief Ottieni ultimo errore MQTT
 * @return Codice ultimo errore, 0 se nessun errore
 */
int32_t handshake_mqtt_get_last_error(void);

// ==================== UTILITY API ====================

/**
 * @brief Crea configurazione MQTT di default
 * 
 * Inizializza struttura config con valori sensati.
 * 
 * @param config Struttura da inizializzare
 * @param broker_uri URI broker MQTT
 * @param topic_prefix Prefisso topic handshake
 * @param client_id Client ID univoco
 */
void handshake_mqtt_get_default_config(handshake_mqtt_config_t *config,
                                      const char *broker_uri,
                                      const char *topic_prefix,
                                      const char *client_id);

/**
 * @brief Genera client ID univoco
 * 
 * Crea client ID nel formato "prefix-MACADDR" per unicità.
 * 
 * @param prefix Prefisso client ID
 * @param client_id_buffer Buffer output per client ID
 * @param buffer_size Dimensione buffer
 * @return ESP_OK se client ID generato
 */
esp_err_t handshake_mqtt_generate_client_id(const char *prefix,
                                           char *client_id_buffer,
                                           size_t buffer_size);

/**
 * @brief Valida configurazione MQTT
 * 
 * Verifica correttezza parametri prima di avvio.
 * 
 * @param config Configurazione da validare
 * @return ESP_OK se configurazione valida
 */
esp_err_t handshake_mqtt_validate_config(const handshake_mqtt_config_t *config);

/**
 * @brief Verifica formato topic MQTT
 * 
 * Controlla validità sintassi topic MQTT.
 * 
 * @param topic Topic da verificare
 * @return true se topic valido
 */
bool handshake_mqtt_validate_topic(const char *topic);

/**
 * @brief Costruisci topic completo
 * 
 * Combina prefisso + suffisso per creare topic completo.
 * 
 * @param prefix Prefisso topic
 * @param suffix Suffisso topic
 * @param full_topic_buffer Buffer output per topic completo
 * @param buffer_size Dimensione buffer
 * @return ESP_OK se topic costruito
 */
esp_err_t handshake_mqtt_build_topic(const char *prefix,
                                    const char *suffix,
                                    char *full_topic_buffer,
                                    size_t buffer_size);

#ifdef __cplusplus
}
#endif