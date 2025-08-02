#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unified Error Management System for ESP32 Firmware Components */

/**
 * @brief Sistema di componenti che possono generare errori
 */
typedef enum {
    ERROR_COMPONENT_SYSTEM = 0,      ///< Errori di sistema generale
    ERROR_COMPONENT_BLE_TRANSPORT,   ///< Transport BLE
    ERROR_COMPONENT_MQTT_TRANSPORT,  ///< Transport MQTT
    ERROR_COMPONENT_CMD_PROCESSOR,   ///< Command processor
    ERROR_COMPONENT_CHUNK_MANAGER,   ///< Chunk manager
    ERROR_COMPONENT_WIFI,            ///< WiFi component
    ERROR_COMPONENT_SOLENOID,        ///< Solenoid control
    ERROR_COMPONENT_SCHEDULE,        ///< Scheduler component
    ERROR_COMPONENT_CODEC,           ///< Codec component
    ERROR_COMPONENT_MAX              ///< Sentinel
} error_component_t;

/**
 * @brief Categorie di errori unificate
 */
typedef enum {
    ERROR_CATEGORY_NONE = 0,         ///< Nessun errore
    
    // Connection & Communication
    ERROR_CATEGORY_CONNECTION,       ///< Errori di connessione
    ERROR_CATEGORY_COMMUNICATION,    ///< Errori di comunicazione
    ERROR_CATEGORY_PROTOCOL,         ///< Errori di protocollo
    
    // Resources & Memory
    ERROR_CATEGORY_RESOURCE,         ///< Errori di risorse
    ERROR_CATEGORY_MEMORY,           ///< Errori di memoria
    ERROR_CATEGORY_QUEUE,            ///< Errori di code
    
    // Processing & Logic
    ERROR_CATEGORY_PROCESSING,       ///< Errori di elaborazione
    ERROR_CATEGORY_VALIDATION,       ///< Errori di validazione
    ERROR_CATEGORY_TIMEOUT,          ///< Errori di timeout
    
    // Hardware & System
    ERROR_CATEGORY_HARDWARE,         ///< Errori hardware
    ERROR_CATEGORY_SYSTEM,           ///< Errori di sistema
    ERROR_CATEGORY_CONFIGURATION,    ///< Errori di configurazione
    
    // Recovery & Management
    ERROR_CATEGORY_RECOVERY,         ///< Errori di recovery
    
    ERROR_CATEGORY_MAX               ///< Sentinel
} error_category_t;

/**
 * @brief Severità dell'errore (unificata)
 */
typedef enum {
    ERROR_SEVERITY_INFO = 0,         ///< Informativo, nessuna azione richiesta
    ERROR_SEVERITY_WARNING,          ///< Warning, possibile degradazione
    ERROR_SEVERITY_ERROR,            ///< Errore, richiede intervento
    ERROR_SEVERITY_CRITICAL,         ///< Critico, sistema instabile
    ERROR_SEVERITY_FATAL             ///< Fatale, richiede restart
} error_severity_t;

/**
 * @brief Strategia di recovery unificata
 */
typedef enum {
    ERROR_RECOVERY_NONE = 0,         ///< Nessuna azione automatica
    ERROR_RECOVERY_RETRY,            ///< Riprova operazione
    ERROR_RECOVERY_RESET_STATE,      ///< Reset stato componente
    ERROR_RECOVERY_RESTART_COMPONENT,///< Riavvia componente
    ERROR_RECOVERY_RESTART_SERVICE,  ///< Riavvia servizio
    ERROR_RECOVERY_SYSTEM_RESTART,   ///< Restart sistema completo
    ERROR_RECOVERY_CUSTOM            ///< Recovery personalizzato
} error_recovery_strategy_t;

/**
 * @brief Struttura completa per errore unificato
 */
typedef struct {
    error_component_t component;         ///< Componente origine errore
    error_category_t category;           ///< Categoria errore
    error_severity_t severity;           ///< Severità errore
    error_recovery_strategy_t recovery;  ///< Strategia recovery suggerita
    
    uint32_t error_code;                 ///< Codice errore specifico componente
    esp_err_t esp_error_code;            ///< Codice errore ESP32 sottostante
    uint32_t timestamp_ms;               ///< Timestamp errore
    
    uint32_t context_data;               ///< Dati di contesto (handle, ID, etc.)
    char description[80];                ///< Descrizione dettagliata errore
    char component_info[32];             ///< Info aggiuntive componente
} unified_error_info_t;

/**
 * @brief Callback per notifica errori unificata
 * 
 * @param error_info Informazioni complete sull'errore
 * @param user_data Data utente passata durante registrazione callback
 */
typedef void (*unified_error_callback_t)(const unified_error_info_t *error_info, void *user_data);

/**
 * @brief Recovery callback personalizzato per componente
 * 
 * @param error_info Informazioni errore
 * @param user_data Data utente del componente
 * @return esp_err_t ESP_OK se recovery riuscito
 */
typedef esp_err_t (*component_recovery_callback_t)(const unified_error_info_t *error_info, void *user_data);

/**
 * @brief Statistiche errori per componente
 */
typedef struct {
    uint32_t total_errors;               ///< Totale errori componente
    uint32_t errors_by_category[ERROR_CATEGORY_MAX]; ///< Errori per categoria
    uint32_t errors_by_severity[5];     ///< Errori per severità
    uint32_t recovery_attempts;          ///< Tentativi recovery
    uint32_t recovery_successes;         ///< Recovery riusciti
    uint32_t last_error_timestamp_ms;    ///< Timestamp ultimo errore
    uint32_t last_error_code;            ///< Ultimo codice errore
    error_category_t last_error_category;///< Ultima categoria errore
} component_error_stats_t;

/**
 * @brief Statistiche globali sistema
 */
typedef struct {
    uint32_t total_system_errors;        ///< Totale errori sistema
    uint32_t errors_by_component[ERROR_COMPONENT_MAX]; ///< Errori per componente
    uint32_t total_recovery_attempts;    ///< Totale tentativi recovery
    uint32_t total_recovery_successes;   ///< Totale recovery riusciti
    uint32_t system_uptime_ms;           ///< Uptime sistema
    uint32_t last_critical_error_ms;     ///< Timestamp ultimo errore critico
    error_component_t most_error_prone_component; ///< Componente con più errori
} system_error_stats_t;

/**
 * @brief Configurazione recovery per componente
 */
typedef struct {
    uint32_t max_consecutive_errors;     ///< Max errori consecutivi
    uint32_t recovery_cooldown_ms;       ///< Cooldown tra recovery
    uint32_t retry_delay_ms;             ///< Delay tra retry
    bool auto_recovery_enabled;          ///< Abilita recovery automatico
    bool escalate_on_failure;            ///< Escalation su fallimento recovery
} component_recovery_config_t;

/* ──────────────── Core API ──────────────── */

/**
 * @brief Inizializza sistema error management unificato
 * 
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_init(void);

/**
 * @brief Cleanup sistema error management
 * 
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_deinit(void);

/**
 * @brief Registra componente nel sistema error management
 * 
 * @param component Tipo componente
 * @param recovery_config Configurazione recovery (NULL per default)
 * @param recovery_callback Callback recovery personalizzato (opzionale)
 * @param user_data Data utente per callback
 * @return esp_err_t ESP_OK se registrazione riuscita
 */
esp_err_t error_manager_register_component(error_component_t component,
                                          const component_recovery_config_t *recovery_config,
                                          component_recovery_callback_t recovery_callback,
                                          void *user_data);

/**
 * @brief Report errore da componente
 * 
 * @param component Componente origine
 * @param category Categoria errore
 * @param severity Severità errore
 * @param error_code Codice errore specifico componente
 * @param esp_code Codice ESP32 sottostante
 * @param context_data Dati contesto (handle, ID, etc.)
 * @param description Descrizione errore (può essere NULL)
 * @return esp_err_t ESP_OK se errore processato
 */
esp_err_t error_manager_report(error_component_t component,
                              error_category_t category,
                              error_severity_t severity,
                              uint32_t error_code,
                              esp_err_t esp_code,
                              uint32_t context_data,
                              const char *description);

/**
 * @brief Registra callback globale per notifiche errori
 * 
 * @param callback Funzione callback
 * @param user_data Data utente
 * @return esp_err_t ESP_OK se registrazione riuscita
 */
esp_err_t error_manager_register_global_callback(unified_error_callback_t callback, void *user_data);

/**
 * @brief Rimuove callback globale
 * 
 * @return esp_err_t ESP_OK se rimozione riuscita
 */
esp_err_t error_manager_unregister_global_callback(void);

/* ──────────────── Statistics & Monitoring API ──────────────── */

/**
 * @brief Ottiene statistiche errori per componente specifico
 * 
 * @param component Componente
 * @param stats Struttura per ricevere statistiche (output)
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_get_component_stats(error_component_t component, component_error_stats_t *stats);

/**
 * @brief Ottiene statistiche errori globali sistema
 * 
 * @param stats Struttura per ricevere statistiche (output)
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_get_system_stats(system_error_stats_t *stats);

/**
 * @brief Reset statistiche per componente specifico
 * 
 * @param component Componente da resettare
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_reset_component_stats(error_component_t component);

/**
 * @brief Reset statistiche globali sistema
 * 
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_reset_system_stats(void);

/* ──────────────── Recovery & Control API ──────────────── */

/**
 * @brief Forza recovery manuale per componente
 * 
 * @param component Componente target
 * @param strategy Strategia recovery da usare
 * @param force_recovery Forza recovery anche se non raccomandato
 * @return esp_err_t ESP_OK se recovery avviato
 */
esp_err_t error_manager_force_recovery(error_component_t component,
                                      error_recovery_strategy_t strategy,
                                      bool force_recovery);

/**
 * @brief Configura recovery per componente
 * 
 * @param component Componente target
 * @param config Nuova configurazione recovery
 * @return esp_err_t ESP_OK se configurazione accettata
 */
esp_err_t error_manager_configure_component_recovery(error_component_t component,
                                                    const component_recovery_config_t *config);

/**
 * @brief Abilita/disabilita recovery automatico per componente
 * 
 * @param component Componente target
 * @param enabled True per abilitare, false per disabilitare
 * @return esp_err_t ESP_OK se successo
 */
esp_err_t error_manager_set_auto_recovery(error_component_t component, bool enabled);

/* ──────────────── Utility API ──────────────── */

/**
 * @brief Ottiene nome componente come stringa
 * 
 * @param component Componente
 * @return const char* Nome componente (sempre valido)
 */
const char* error_manager_get_component_name(error_component_t component);

/**
 * @brief Ottiene nome categoria come stringa
 * 
 * @param category Categoria errore
 * @return const char* Nome categoria (sempre valido)
 */
const char* error_manager_get_category_name(error_category_t category);

/**
 * @brief Ottiene nome severità come stringa
 * 
 * @param severity Severità errore
 * @return const char* Nome severità (sempre valido)
 */
const char* error_manager_get_severity_name(error_severity_t severity);

/**
 * @brief Ottiene descrizione strategia recovery
 * 
 * @param strategy Strategia recovery
 * @return const char* Descrizione strategia (sempre valido)
 */
const char* error_manager_get_recovery_description(error_recovery_strategy_t strategy);

/* ──────────────── System Health API ──────────────── */

/**
 * @brief Verifica salute generale sistema
 * 
 * @return error_severity_t Livello massimo di severità presente nel sistema
 */
error_severity_t error_manager_get_system_health(void);

/**
 * @brief Verifica se componente è in stato degradato
 * 
 * @param component Componente da verificare
 * @return bool True se componente ha errori critici recenti
 */
bool error_manager_is_component_degraded(error_component_t component);

/**
 * @brief Ottiene tempo dall'ultimo errore critico sistema
 * 
 * @return uint32_t Millisecondi dall'ultimo errore critico (0 se mai avvenuto)
 */
uint32_t error_manager_time_since_last_critical_error(void);

#ifdef __cplusplus
}
#endif