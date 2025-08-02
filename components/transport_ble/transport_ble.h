
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
 * @brief Stato del transport BLE
 */
typedef enum {
    BLE_DOWN = 0,     ///< BLE non inizializzato o fermato
    BLE_STARTING,     ///< Inizializzazione BLE stack in corso
    BLE_ADVERTISING,  ///< In advertising, nessun client connesso
    BLE_UP,          ///< Client connesso, GATT operativo e funzionante
    BLE_BUSY,        ///< Operazione in corso (MTU exchange, pairing)
    BLE_ERROR        ///< Errore critico, richiede restart
} ble_state_t;

/**
 * @brief Configurazione chunking per frammentazione BLE
 */
typedef struct {
    uint16_t max_chunk_size;    ///< Dimensione massima chunk (default: ATT_MTU - 3)
    uint8_t max_concurrent;     ///< Max stream concorrenti (default: 4)
    uint32_t reassembly_timeout_ms; ///< Timeout riassemblaggio (default: 2000ms)
} ble_chunk_config_t;


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

/* Legacy API - backward compatibility */
void smart_ble_transport_init(QueueHandle_t cmd_q, QueueHandle_t resp_q) __attribute__((deprecated("Use transport_ble_init")));

#ifdef __cplusplus
}
#endif
