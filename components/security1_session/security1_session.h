/**
 * @file security1_session.h
 * @brief Security1 Session Management Framework
 * 
 * Fornisce un framework unificato per gestire sessioni Security1 (X25519 + PoP)
 * attraverso diversi transport (BLE, MQTT, HTTPD) utilizzando il protocomm ESP-IDF.
 * 
 * Caratteristiche:
 * - Transport-agnostic: BLE, MQTT, HTTPD con stessa API
 * - Dual-channel: handshake su canale standard + traffico operativo su canale custom
 * - Thread-safe con mutex protection
 * - Integration con error_manager framework
 * - Statistics e diagnostics complete
 * - AES-CTR + HMAC-SHA256 encryption
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== CONSTANTS ====================

#define SECURITY1_SESSION_KEY_SIZE      32
#define SECURITY1_SESSION_IV_SIZE       16
#define SECURITY1_MAX_POP_LENGTH        64
#define SECURITY1_MAX_DEVICE_NAME       32
#define SECURITY1_MAX_TOPIC_LENGTH      128
#define SECURITY1_MAX_URI_LENGTH        256

// Overhead encryption: IV (16) + MAC (32) + padding alignment
#define SECURITY1_ENCRYPTION_OVERHEAD   64

// ==================== TYPES ====================

/**
 * @brief Buffer per dati crittografici
 */
typedef struct {
    uint8_t *data;
    size_t   length;
} security1_buffer_t;

/**
 * @brief Tipi di handshake transport supportati
 */
typedef enum {
    SECURITY1_HANDSHAKE_BLE,        ///< BLE GATT su servizio FF50
    SECURITY1_HANDSHAKE_MQTT,       ///< MQTT su topic proto/rx|tx
    SECURITY1_HANDSHAKE_HTTPD,      ///< HTTP server su Soft-AP
    SECURITY1_HANDSHAKE_CUSTOM      ///< Custom transport implementation
} security1_handshake_type_t;

/**
 * @brief Stati sessione Security1
 */
typedef enum {
    SECURITY1_STATE_IDLE,                ///< Sessione non inizializzata
    SECURITY1_STATE_TRANSPORT_STARTING,  ///< Avvio transport in corso
    SECURITY1_STATE_TRANSPORT_READY,     ///< Transport pronto, attesa handshake
    SECURITY1_STATE_HANDSHAKE_PENDING,   ///< Handshake X25519 in corso
    SECURITY1_STATE_HANDSHAKE_COMPLETE,  ///< Handshake completato, PoP verificato
    SECURITY1_STATE_SESSION_ACTIVE,      ///< Sessione attiva, crypto abilitata
    SECURITY1_STATE_ERROR,               ///< Errore, sessione non utilizzabile
    SECURITY1_STATE_STOPPING            ///< Cleanup in corso
} security1_session_state_t;

/**
 * @brief Callback per eventi sessione
 * @param state Nuovo stato sessione
 * @param user_data User data fornito durante start
 */
typedef void (*security1_event_callback_t)(security1_session_state_t state, void *user_data);

/**
 * @brief Configurazione handshake BLE
 */
typedef struct {
    char device_name[SECURITY1_MAX_DEVICE_NAME];    ///< Nome device in advertising
    uint16_t appearance;                             ///< BLE appearance value
    bool enable_bonding;                             ///< Abilita BLE bonding
    uint16_t max_mtu;                               ///< MTU massimo supportato
} security1_handshake_ble_config_t;

/**
 * @brief Configurazione handshake MQTT
 */
typedef struct {
    char broker_uri[SECURITY1_MAX_URI_LENGTH];       ///< URI broker MQTT
    char topic_prefix[SECURITY1_MAX_TOPIC_LENGTH];   ///< Prefisso topic handshake
    uint8_t qos_level;                              ///< QoS level messaggi
    char client_id[SECURITY1_MAX_DEVICE_NAME];      ///< Client ID MQTT
    uint16_t keepalive_interval;                    ///< Keepalive seconds
} security1_handshake_mqtt_config_t;

/**
 * @brief Configurazione handshake HTTPD
 */
typedef struct {
    uint16_t port;                                  ///< Porta server HTTP
    uint16_t max_sessions;                          ///< Sessioni HTTP max
    bool enable_cors;                               ///< Abilita CORS headers
} security1_handshake_httpd_config_t;

/**
 * @brief Configurazione generica handshake
 */
typedef union {
    security1_handshake_ble_config_t  ble;
    security1_handshake_mqtt_config_t mqtt;
    security1_handshake_httpd_config_t httpd;
    void *custom_config;
} security1_handshake_config_t;

/**
 * @brief Statistiche sessione corrente
 */
typedef struct {
    uint32_t handshake_duration_ms;       ///< Durata handshake completato
    uint32_t session_duration_ms;         ///< Durata totale sessione
    uint64_t bytes_encrypted;             ///< Byte totali cifrati
    uint64_t bytes_decrypted;             ///< Byte totali decifrati
    uint32_t encryption_operations;       ///< Numero operazioni cifratura
    uint32_t decryption_operations;       ///< Numero operazioni decifratura
    uint32_t errors_count;                ///< Numero errori totali
    uint32_t last_activity_timestamp;     ///< Timestamp ultima attività
} security1_session_stats_t;

/**
 * @brief Informazioni sessione corrente
 */
typedef struct {
    security1_session_state_t state;
    security1_handshake_type_t handshake_type;
    char pop_hash[16];                    ///< Hash PoP per identificazione
    uint32_t session_start_time;          ///< Timestamp inizio sessione
    bool session_key_valid;               ///< Session key derivata e valida
} security1_session_info_t;

// ==================== ERROR CODES ====================

#define SECURITY1_ERROR_BASE                    0x8000
#define SECURITY1_ERROR_INVALID_PARAMETER       (SECURITY1_ERROR_BASE + 1)
#define SECURITY1_ERROR_INVALID_STATE           (SECURITY1_ERROR_BASE + 2)
#define SECURITY1_ERROR_HANDSHAKE_FAILED        (SECURITY1_ERROR_BASE + 3)
#define SECURITY1_ERROR_ENCRYPTION_FAILED       (SECURITY1_ERROR_BASE + 4)
#define SECURITY1_ERROR_DECRYPTION_FAILED       (SECURITY1_ERROR_BASE + 5)
#define SECURITY1_ERROR_TRANSPORT_FAILED        (SECURITY1_ERROR_BASE + 6)
#define SECURITY1_ERROR_MUTEX_TIMEOUT           (SECURITY1_ERROR_BASE + 7)
#define SECURITY1_ERROR_MEMORY_ALLOCATION       (SECURITY1_ERROR_BASE + 8)
#define SECURITY1_ERROR_PROTOCOMM_FAILED        (SECURITY1_ERROR_BASE + 9)
#define SECURITY1_ERROR_SESSION_EXPIRED         (SECURITY1_ERROR_BASE + 10)

// ==================== CORE API ====================

/**
 * @brief Inizializza il framework security1_session
 * 
 * Deve essere chiamato una volta all'avvio del sistema prima di usare
 * qualsiasi altra API del framework.
 * 
 * @return ESP_OK se inizializzazione riuscita
 */
esp_err_t security1_session_init(void);

/**
 * @brief Avvia sessione Security1 su transport specificato
 * 
 * Configura il transport scelto (BLE/MQTT/HTTPD), avvia il handshake
 * Security1 e attende la derivazione della session key.
 * 
 * @param handshake_type Tipo di transport per handshake
 * @param handshake_config Configurazione transport-specific
 * @param proof_of_possession Stringa PoP per autenticazione (max 64 char)
 * @param event_callback Callback per eventi sessione (opzionale)
 * @param user_data User data per callback (opzionale)
 * @return ESP_OK se handshake avviato correttamente
 */
esp_err_t security1_session_start(security1_handshake_type_t handshake_type,
                                  const security1_handshake_config_t *handshake_config,
                                  const char *proof_of_possession,
                                  security1_event_callback_t event_callback,
                                  void *user_data);

/**
 * @brief Ferma sessione Security1 e rilascia tutte le risorse
 * 
 * Chiude il transport, invalida la session key e libera memoria.
 * Dopo questa chiamata è necessario un nuovo start() per riutilizzare.
 * 
 * @return ESP_OK se cleanup completato
 */
esp_err_t security1_session_stop(void);

/**
 * @brief Ottieni stato corrente sessione
 * @return Stato attuale della sessione
 */
security1_session_state_t security1_session_get_state(void);

/**
 * @brief Verifica se sessione è attiva e pronta per operazioni crypto
 * @return true se sessione in stato SESSION_ACTIVE
 */
bool security1_session_is_active(void);

/**
 * @brief Verifica se handshake è completato
 * @return true se handshake completato con successo
 */
bool security1_session_is_handshake_complete(void);

/**
 * @brief Ottieni informazioni dettagliate sessione corrente
 * @param info Buffer output per informazioni sessione
 * @return ESP_OK se informazioni ottenute
 */
esp_err_t security1_session_get_info(security1_session_info_t *info);

// ==================== CRYPTO API ====================

/**
 * @brief Cifra dati usando session key corrente
 * 
 * Utilizza AES-CTR + HMAC-SHA256 con session key derivata dall'handshake.
 * Il buffer output deve essere allocato dal caller con dimensione ottenuta
 * da security1_get_encrypted_size().
 * 
 * @param plaintext Dati in chiaro da cifrare
 * @param ciphertext Buffer output per dati cifrati (pre-allocato)
 * @return ESP_OK se cifratura riuscita
 */
esp_err_t security1_encrypt(const security1_buffer_t *plaintext, 
                           security1_buffer_t *ciphertext);

/**
 * @brief Decifra dati usando session key corrente
 * 
 * Verifica HMAC e decifra con AES-CTR. Il buffer output deve essere
 * allocato dal caller con dimensione ottenuta da security1_get_decrypted_size().
 * 
 * @param ciphertext Dati cifrati da decifrare
 * @param plaintext Buffer output per dati in chiaro (pre-allocato)
 * @return ESP_OK se decifratura e verifica HMAC riuscite
 */
esp_err_t security1_decrypt(const security1_buffer_t *ciphertext,
                           security1_buffer_t *plaintext);

/**
 * @brief Processa messaggio handshake ricevuto via MQTT
 * 
 * Inoltra il messaggio handshake al framework protocomm per elaborazione.
 * Utilizzato dal transport MQTT per delegare messaggi handshake al Security1.
 * 
 * @param handshake_data Buffer contenente dati handshake ricevuti
 * @param data_length Lunghezza dati handshake
 * @param response_topic Topic MQTT per inviare risposta handshake
 * @return ESP_OK se messaggio processato correttamente
 */
esp_err_t security1_process_handshake_message(const uint8_t *handshake_data,
                                             size_t data_length,
                                             const char *response_topic);

/**
 * @brief Calcola dimensione buffer necessaria per dati cifrati
 * 
 * Include overhead per IV, MAC e padding alignment.
 * 
 * @param plaintext_length Lunghezza dati in chiaro
 * @return Dimensione buffer necessaria per ciphertext
 */
size_t security1_get_encrypted_size(size_t plaintext_length);

/**
 * @brief Calcola dimensione buffer necessaria per dati in chiaro
 * 
 * Rimuove overhead IV, MAC e padding.
 * 
 * @param ciphertext_length Lunghezza dati cifrati
 * @return Dimensione buffer necessaria per plaintext
 */
size_t security1_get_decrypted_size(size_t ciphertext_length);

// ==================== DIAGNOSTICS API ====================

/**
 * @brief Ottieni session key corrente (solo per debug/test)
 * 
 * ATTENZIONE: Espone materiale crittografico sensibile.
 * Usare solo per debug o unit test.
 * 
 * @param key_buffer Buffer output per session key (32 bytes)
 * @return ESP_OK se session key disponibile
 */
esp_err_t security1_get_session_key(uint8_t key_buffer[SECURITY1_SESSION_KEY_SIZE]);

/**
 * @brief Ottieni statistiche sessione corrente
 * @param stats Buffer output per statistiche
 * @return ESP_OK se statistiche ottenute
 */
esp_err_t security1_get_session_stats(security1_session_stats_t *stats);

/**
 * @brief Reset statistiche sessione
 * 
 * Azzera contatori ma mantiene sessione attiva.
 */
void security1_reset_session_stats(void);

/**
 * @brief Verifica validità session key
 * @return true se session key valida e utilizzabile
 */
bool security1_is_session_key_valid(void);

/**
 * @brief Ottieni timestamp ultima attività crypto
 * @return Timestamp ultima encrypt/decrypt operation
 */
uint32_t security1_get_last_activity_timestamp(void);

// ==================== UTILITY API ====================

/**
 * @brief Genera PoP automatico da MAC address WiFi
 * 
 * Crea stringa PoP nel formato "AABBCCDDEEFF" usando MAC WiFi STA.
 * 
 * @param pop_buffer Buffer output per PoP generato
 * @param buffer_size Dimensione buffer (minimo 13 caratteri)
 * @return ESP_OK se generazione riuscita
 */
esp_err_t security1_generate_pop_from_mac(char *pop_buffer, size_t buffer_size);

/**
 * @brief Genera PoP personalizzato con prefisso
 * 
 * Crea PoP nel formato "PREFIX-AABBCC" combinando prefisso e MAC.
 * 
 * @param prefix Prefisso stringa (max 8 caratteri)
 * @param pop_buffer Buffer output per PoP generato
 * @param buffer_size Dimensione buffer
 * @return ESP_OK se generazione riuscita
 */
esp_err_t security1_generate_pop_with_prefix(const char *prefix, 
                                            char *pop_buffer, 
                                            size_t buffer_size);

/**
 * @brief Verifica validità formato PoP
 * 
 * Controlla lunghezza e caratteri validi per PoP string.
 * 
 * @param proof_of_possession Stringa PoP da verificare
 * @return true se formato valido
 */
bool security1_validate_pop_format(const char *proof_of_possession);

/**
 * @brief Ottieni nome human-readable per transport type
 * @param handshake_type Tipo transport
 * @return Stringa descrittiva (es. "BLE", "MQTT")
 */
const char *security1_get_transport_name(security1_handshake_type_t handshake_type);

/**
 * @brief Ottieni nome human-readable per stato sessione
 * @param state Stato sessione
 * @return Stringa descrittiva stato
 */
const char *security1_get_state_name(security1_session_state_t state);

// ==================== CLEANUP ====================

/**
 * @brief Cleanup completo framework security1_session
 * 
 * Ferma eventuali sessioni attive, libera memoria globale.
 * Chiamare alla shutdown del sistema.
 */
void security1_session_deinit(void);

#ifdef __cplusplus
}
#endif