/**
 * @file handshake_ble.h
 * @brief BLE Handshake Transport per Security1 Session
 * 
 * Wrapper per protocomm_ble ESP-IDF che fornisce handshake Security1
 * su servizio BLE standard (FF50). Gestisce advertising, GATT service
 * e integration con protocomm core.
 */

#pragma once

#include "security1_session.h"
#include "protocomm.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== BLE CONSTANTS ====================

// UUID standard protocomm per compatibility con SDK Espressif
#define HANDSHAKE_BLE_SERVICE_UUID        "0000ff50-0000-1000-8000-00805f9b34fb"
#define HANDSHAKE_BLE_RX_CHAR_UUID        "0000ff51-0000-1000-8000-00805f9b34fb"
#define HANDSHAKE_BLE_TX_CHAR_UUID        "0000ff52-0000-1000-8000-00805f9b34fb"

#define HANDSHAKE_BLE_DEFAULT_MTU         512
#define HANDSHAKE_BLE_MIN_MTU             23
#define HANDSHAKE_BLE_MAX_MTU             2048

// ==================== TYPES ====================

/**
 * @brief Stati specifici handshake BLE
 */
typedef enum {
    HANDSHAKE_BLE_STATE_IDLE,           ///< Non inizializzato
    HANDSHAKE_BLE_STATE_STARTING,       ///< Avvio stack BLE
    HANDSHAKE_BLE_STATE_ADVERTISING,    ///< Advertising attivo
    HANDSHAKE_BLE_STATE_CONNECTED,      ///< Client connesso
    HANDSHAKE_BLE_STATE_HANDSHAKING,    ///< Handshake Security1 in corso
    HANDSHAKE_BLE_STATE_READY,          ///< Handshake completato
    HANDSHAKE_BLE_STATE_ERROR           ///< Errore BLE
} handshake_ble_state_t;

/**
 * @brief Callback per eventi specifici BLE
 * @param state Stato BLE corrente
 * @param event_data Dati evento (opzionale)
 * @param user_data User data fornito durante init
 */
typedef void (*handshake_ble_event_callback_t)(handshake_ble_state_t state, 
                                               void *event_data, 
                                               void *user_data);

/**
 * @brief Configurazione completa handshake BLE
 */
typedef struct {
    // Configurazione advertising
    char device_name[SECURITY1_MAX_DEVICE_NAME];    ///< Nome in advertising
    uint16_t appearance;                             ///< BLE appearance
    bool enable_bonding;                             ///< Abilita bonding
    uint16_t advertising_interval_min;               ///< Intervallo adv min (ms)
    uint16_t advertising_interval_max;               ///< Intervallo adv max (ms)
    
    // Configurazione GATT
    uint16_t max_mtu;                               ///< MTU massimo accettato
    uint16_t connection_timeout;                    ///< Timeout connessione (ms)
    
    // Manufacturer data per advertising (opzionale)
    uint8_t *manufacturer_data;                     ///< Dati manufacturer
    size_t manufacturer_data_len;                   ///< Lunghezza dati
    
    // Callbacks eventi BLE (opzionale)
    handshake_ble_event_callback_t event_callback;  ///< Callback eventi BLE
    void *user_data;                                ///< User data callback
} handshake_ble_config_t;

/**
 * @brief Statistiche specifiche BLE
 */
typedef struct {
    uint32_t advertising_duration_ms;      ///< Tempo totale advertising
    uint32_t connection_count;             ///< Numero connessioni totali
    uint32_t disconnection_count;          ///< Numero disconnessioni
    uint32_t handshake_attempts;           ///< Tentativi handshake
    uint32_t handshake_successes;          ///< Handshake riusciti
    uint16_t current_mtu;                  ///< MTU corrente negoziato
    int8_t current_rssi;                   ///< RSSI corrente connessione
    char connected_client_address[18];     ///< MAC address client connesso
} handshake_ble_stats_t;

// ==================== CORE BLE API ====================

/**
 * @brief Avvia handshake Security1 su BLE
 * 
 * Configura stack NimBLE, crea servizio GATT FF50, avvia advertising
 * e prepara endpoint protocomm per handshake.
 * 
 * @param pc Istanza protocomm da utilizzare
 * @param config Configurazione BLE specifica
 * @return ESP_OK se BLE avviato correttamente
 */
esp_err_t handshake_ble_start(protocomm_t *pc, const handshake_ble_config_t *config);

/**
 * @brief Ferma handshake BLE e cleanup risorse
 * 
 * Ferma advertising, disconnette client, libera risorse NimBLE.
 * 
 * @param pc Istanza protocomm utilizzata
 * @return ESP_OK se cleanup completato
 */
esp_err_t handshake_ble_stop(protocomm_t *pc);

/**
 * @brief Verifica se handshake BLE è attivo
 * @param pc Istanza protocomm
 * @return true se BLE attivo e advertising
 */
bool handshake_ble_is_active(protocomm_t *pc);

/**
 * @brief Verifica se client è connesso
 * @return true se client BLE connesso
 */
bool handshake_ble_is_connected(void);

/**
 * @brief Ottieni stato corrente handshake BLE
 * @return Stato BLE corrente
 */
handshake_ble_state_t handshake_ble_get_state(void);

// ==================== BLE MANAGEMENT API ====================

/**
 * @brief Avvia advertising BLE
 * 
 * Inizia advertising con configurazione fornita durante start().
 * 
 * @return ESP_OK se advertising avviato
 */
esp_err_t handshake_ble_start_advertising(void);

/**
 * @brief Ferma advertising BLE
 * 
 * Ferma advertising ma mantiene servizio GATT attivo.
 * 
 * @return ESP_OK se advertising fermato
 */
esp_err_t handshake_ble_stop_advertising(void);

/**
 * @brief Disconnetti client corrente
 * 
 * Forza disconnessione client BLE se connesso.
 * 
 * @return ESP_OK se disconnessione avviata
 */
esp_err_t handshake_ble_disconnect_client(void);

/**
 * @brief Riavvia advertising dopo disconnessione
 * 
 * Utility per riavviare advertising dopo handshake completato.
 * 
 * @return ESP_OK se riavvio riuscito
 */
esp_err_t handshake_ble_restart_advertising(void);

// ==================== CONFIGURATION API ====================

/**
 * @brief Aggiorna configurazione advertising
 * 
 * Modifica parametri advertising durante runtime.
 * 
 * @param device_name Nuovo nome device (NULL per non modificare)
 * @param interval_min Nuovo intervallo min ms (0 per non modificare)
 * @param interval_max Nuovo intervallo max ms (0 per non modificare)
 * @return ESP_OK se aggiornamento riuscito
 */
esp_err_t handshake_ble_update_advertising(const char *device_name,
                                          uint16_t interval_min,
                                          uint16_t interval_max);

/**
 * @brief Aggiorna manufacturer data in advertising
 * 
 * @param manufacturer_data Nuovi dati manufacturer (NULL per rimuovere)
 * @param data_len Lunghezza dati
 * @return ESP_OK se aggiornamento riuscito
 */
esp_err_t handshake_ble_update_manufacturer_data(const uint8_t *manufacturer_data,
                                                size_t data_len);

/**
 * @brief Configura timeout connessione
 * 
 * @param timeout_ms Nuovo timeout in millisecondi
 * @return ESP_OK se configurazione applicata
 */
esp_err_t handshake_ble_set_connection_timeout(uint16_t timeout_ms);

// ==================== DIAGNOSTICS API ====================

/**
 * @brief Ottieni statistiche BLE correnti
 * @param stats Buffer output per statistiche
 * @return ESP_OK se statistiche ottenute
 */
esp_err_t handshake_ble_get_stats(handshake_ble_stats_t *stats);

/**
 * @brief Reset statistiche BLE
 * 
 * Azzera contatori ma mantiene connessione attiva.
 */
void handshake_ble_reset_stats(void);

/**
 * @brief Ottieni MTU corrente negoziato
 * @return MTU in bytes, 0 se non connesso
 */
uint16_t handshake_ble_get_current_mtu(void);

/**
 * @brief Ottieni RSSI connessione corrente
 * @return RSSI in dBm, 0 se non connesso
 */
int8_t handshake_ble_get_current_rssi(void);

/**
 * @brief Ottieni MAC address client connesso
 * @param address_buffer Buffer output per MAC (formato XX:XX:XX:XX:XX:XX)
 * @param buffer_size Dimensione buffer (minimo 18 caratteri)
 * @return ESP_OK se client connesso e MAC ottenuto
 */
esp_err_t handshake_ble_get_client_address(char *address_buffer, size_t buffer_size);

// ==================== UTILITY API ====================

/**
 * @brief Crea configurazione BLE di default
 * 
 * Inizializza struttura config con valori sensati.
 * 
 * @param config Struttura da inizializzare
 * @param device_name Nome device per advertising
 */
void handshake_ble_get_default_config(handshake_ble_config_t *config, 
                                     const char *device_name);

/**
 * @brief Verifica se BLE è supportato su questo hardware
 * @return true se BLE disponibile
 */
bool handshake_ble_is_supported(void);

/**
 * @brief Ottieni versione driver BLE utilizzato
 * @return Stringa versione (es. "NimBLE 1.4.0")
 */
const char *handshake_ble_get_driver_version(void);

/**
 * @brief Valida configurazione BLE
 * 
 * Verifica correttezza parametri prima di avvio.
 * 
 * @param config Configurazione da validare
 * @return ESP_OK se configurazione valida
 */
esp_err_t handshake_ble_validate_config(const handshake_ble_config_t *config);

#ifdef __cplusplus
}
#endif