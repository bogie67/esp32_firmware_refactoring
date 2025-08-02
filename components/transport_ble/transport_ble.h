
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BLE transport component - API unified with transport_mqtt */

/**
 * @brief Stato del transport BLE (Enhanced per Security1)
 */
typedef enum {
    BLE_DOWN = 0,                    ///< BLE non inizializzato o fermato
    BLE_STARTING,                    ///< Inizializzazione BLE stack in corso
    BLE_ADVERTISING,                 ///< In advertising, nessun client connesso
    BLE_UP,                         ///< Client connesso, GATT operativo e funzionante
    BLE_BUSY,                       ///< Operazione in corso (MTU exchange, pairing)
    
    // NEW: Security1 states
    BLE_SECURITY1_HANDSHAKE,        ///< Security1 handshake in corso (FF50-FF52)
    BLE_SECURITY1_READY,            ///< Security1 session stabilita
    BLE_OPERATIONAL,                ///< Servizio operativo attivo (FF00-FF02)
    BLE_ENCRYPTED_COMM,             ///< Comunicazione crittografata attiva
    
    BLE_ERROR                       ///< Errore critico, richiede restart
} ble_state_t;

/**
 * @brief Configurazione chunking per frammentazione BLE
 */
typedef struct {
    uint16_t max_chunk_size;    ///< Dimensione massima chunk (default: ATT_MTU - 3)
    uint8_t max_concurrent;     ///< Max stream concorrenti (default: 4)
    uint32_t reassembly_timeout_ms; ///< Timeout riassemblaggio (default: 2000ms)
} ble_chunk_config_t;

/* ──────────────── BLE Error Types (For Framework Integration) ──────────────── */

/**
 * @brief Tipi di errore BLE per mappatura al framework unificato
 * @note Usati internamente per mappare agli error_category_t del framework
 */
typedef enum {
    BLE_ERROR_NONE = 0,
    BLE_ERROR_CONNECTION_LOST, BLE_ERROR_CONNECTION_FAILED, BLE_ERROR_CONNECTION_TIMEOUT,
    BLE_ERROR_MTU_NEGOTIATION, BLE_ERROR_GATT_WRITE_FAILED, BLE_ERROR_GATT_READ_FAILED, BLE_ERROR_NOTIFICATION_FAILED,
    BLE_ERROR_MEMORY_EXHAUSTED, BLE_ERROR_QUEUE_FULL, BLE_ERROR_RESOURCE_UNAVAILABLE,
    BLE_ERROR_CHUNK_ASSEMBLY_FAILED, BLE_ERROR_CHUNK_TIMEOUT, BLE_ERROR_INVALID_FRAME, BLE_ERROR_PROTOCOL_VIOLATION,
    BLE_ERROR_STACK_FAULT, BLE_ERROR_HARDWARE_FAULT, BLE_ERROR_CONFIGURATION_INVALID,
    BLE_ERROR_RECOVERY_FAILED, BLE_ERROR_RESTART_REQUIRED,
    BLE_ERROR_MAX
} ble_error_type_t;

typedef enum {
    BLE_ERROR_SEVERITY_INFO = 0, BLE_ERROR_SEVERITY_WARNING, BLE_ERROR_SEVERITY_ERROR, BLE_ERROR_SEVERITY_CRITICAL
} ble_error_severity_t;


/**
 * @brief Inizializza il transport BLE con code di comando e risposta
 * 
 * @param cmdQueue Queue per ricevere comandi dal client BLE
 * @param respQueue Queue per inviare risposte al client BLE
 */
void transport_ble_init(QueueHandle_t cmdQueue, QueueHandle_t respQueue);

/**
 * @brief Avvia il BLE stack e inizia advertising
 * 
 * Sequenza: NimBLE host start → GATT services → Advertising
 */
void transport_ble_start(void);

/**
 * @brief Ferma advertising e disconnette client attivi
 * 
 * Mantiene il BLE stack attivo per restart rapido
 */
void transport_ble_stop(void);

/**
 * @brief Ottieni stato connessione BLE (legacy compatibility)
 * 
 * @return true se client connesso (BLE_UP), false altrimenti
 */
bool transport_ble_is_connected(void);

/**
 * @brief Ottiene lo stato dettagliato del transport BLE
 * 
 * @return ble_state_t Stato corrente (BLE_DOWN/STARTING/ADVERTISING/UP/BUSY/ERROR)
 */
ble_state_t transport_ble_get_state(void);

/**
 * @brief Cleanup completo risorse BLE
 * 
 * Libera memoria, ferma tasks, deinit NimBLE stack
 */
void transport_ble_cleanup(void);

/* ──────────────── Security1 Integration API ──────────────── */

/**
 * @brief Configurazione Security1 per transport BLE
 */
typedef struct {
    char device_name[32];              ///< Nome device per advertising
    char proof_of_possession[64];      ///< PoP per handshake Security1
    uint16_t handshake_timeout_ms;     ///< Timeout handshake (default: 30000ms)
    bool enable_encryption;            ///< Abilita crittografia sessione
    bool fallback_to_legacy;          ///< Fallback a modalità legacy se handshake fallisce
} transport_ble_security1_config_t;

/**
 * @brief Avvia transport BLE con supporto Security1 (Dual Service)
 * 
 * Inizializza sia il servizio handshake (FF50-FF52) che operativo (FF00-FF02)
 * 
 * @param cmdQueue Queue per comandi
 * @param respQueue Queue per risposte  
 * @param sec1_config Configurazione Security1
 * @return esp_err_t ESP_OK se avvio riuscito
 */
esp_err_t transport_ble_start_with_security1(QueueHandle_t cmdQueue, 
                                            QueueHandle_t respQueue,
                                            const transport_ble_security1_config_t *sec1_config);

/**
 * @brief Invia dati crittografati via BLE
 * 
 * Utilizza la sessione Security1 per crittografare i dati prima dell'invio
 * 
 * @param data Dati da inviare
 * @param len Lunghezza dati
 * @return esp_err_t ESP_OK se invio riuscito
 */
esp_err_t transport_ble_send_encrypted(const uint8_t *data, size_t len);

/**
 * @brief Verifica se Security1 è attivo
 * 
 * @return true se sessione Security1 stabilita e operativa
 */
bool transport_ble_is_security1_active(void);

/**
 * @brief Ottieni informazioni sessione Security1
 * 
 * @param session_established Output: true se sessione stabilita
 * @param encryption_active Output: true se crittografia attiva
 * @param handshake_service_active Output: true se servizio FF50-FF52 attivo
 * @param operational_service_active Output: true se servizio FF00-FF02 attivo
 * @return esp_err_t ESP_OK se informazioni ottenute
 */
esp_err_t transport_ble_get_security1_info(bool *session_established,
                                          bool *encryption_active, 
                                          bool *handshake_service_active,
                                          bool *operational_service_active);

/**
 * @brief Forza transizione a modalità operativa
 * 
 * Switch dal servizio handshake (FF50-FF52) al servizio operativo (FF00-FF02)
 * 
 * @return esp_err_t ESP_OK se transizione riuscita
 */
esp_err_t transport_ble_transition_to_operational(void);

/**
 * @brief Configura parametri di chunking (opzionale)
 * 
 * @param config Configurazione chunking, NULL per default
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t transport_ble_set_chunk_config(const ble_chunk_config_t *config);

/**
 * @brief Ottiene statistiche di connessione BLE
 * 
 * @param conn_handle Handle connessione attiva (output)
 * @param mtu MTU negoziato corrente (output)  
 * @param chunks_pending Chunk in attesa di riassemblaggio (output)
 * @return esp_err_t ESP_OK se client connesso
 */
esp_err_t transport_ble_get_connection_info(uint16_t *conn_handle, 
                                          uint16_t *mtu, 
                                          uint8_t *chunks_pending);

/* ──────────────── Error Handling (Unified Framework) ──────────────── */

/**
 * @brief Ottiene descrizione testuale per tipo errore
 * @note Per error handling completo, usare le API error_manager_*
 */
const char* transport_ble_get_error_description(ble_error_type_t error_type);

/* Legacy API - backward compatibility */
void smart_ble_transport_init(QueueHandle_t cmd_q, QueueHandle_t resp_q) __attribute__((deprecated("Use transport_ble_init")));

#ifdef __cplusplus
}
#endif
