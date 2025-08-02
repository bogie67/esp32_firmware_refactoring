#include "transport_mqtt.h"
#include "error_manager.h"
#include "security1_session.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "codec.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "MQTT_TR";

// Client MQTT e configurazione
static esp_mqtt_client_handle_t mqtt_client = NULL;
static QueueHandle_t cmd_queue = NULL;
static QueueHandle_t resp_queue = NULL;

// State machine e reconnection logic
static mqtt_state_t mqtt_state = MQTT_DOWN;
static esp_timer_handle_t reconnect_timer = NULL;
static uint32_t backoff_delay_ms = CONFIG_MQTT_BACKOFF_INITIAL_MS;

// Task handle per il task TX
static TaskHandle_t tx_task_handle = NULL;

/* ────────────── Security1 Integration Variables ────────────── */

// Security1 session state
static bool security1_session_active = false;
static bool security1_encryption_enabled = false;
static bool handshake_topics_active = false;
static bool operational_topics_active = false;
static transport_mqtt_security1_config_t security1_config = {0};

// Topic buffers for dual-topic architecture
static char handshake_rx_topic[128] = {0};
static char handshake_tx_topic[128] = {0};
static char operational_rx_topic[128] = {0};
static char operational_tx_topic[128] = {0};

/* ────────────── Security1 Integration Functions ────────────── */

/**
 * @brief Security1 event callback per gestione stati dual-topic
 */
static void transport_mqtt_security1_event_callback(security1_session_state_t state, void *user_data)
{
    ESP_LOGI(TAG, "🔐 Security1 event: %d", state);
    
    switch (state) {
        case SECURITY1_STATE_TRANSPORT_READY:
            ESP_LOGI(TAG, "🤝 Security1 transport pronto su topic %s", handshake_rx_topic);
            mqtt_state = MQTT_SECURITY1_HANDSHAKE;
            handshake_topics_active = true;
            break;
            
        case SECURITY1_STATE_HANDSHAKE_PENDING:
            ESP_LOGI(TAG, "🔄 Security1 handshake in corso");
            mqtt_state = MQTT_SECURITY1_HANDSHAKE;
            break;
            
        case SECURITY1_STATE_HANDSHAKE_COMPLETE:
            ESP_LOGI(TAG, "✅ Security1 handshake completato - transizione a modalità operativa");
            
            // Transition to operational topics
            if (transport_mqtt_transition_to_operational() == ESP_OK) {
                mqtt_state = MQTT_OPERATIONAL;
                operational_topics_active = true;
                security1_session_active = true;
                
            } else {
                ESP_LOGE(TAG, "❌ Fallito transition to operational");
                mqtt_state = MQTT_DOWN;
            }
            break;
            
        case SECURITY1_STATE_SESSION_ACTIVE:
            ESP_LOGI(TAG, "🔒 Security1 sessione attiva - crittografia abilitata");
            mqtt_state = MQTT_ENCRYPTED_COMM;
            security1_encryption_enabled = true;
            break;
            
        case SECURITY1_STATE_STOPPING:
            ESP_LOGW(TAG, "⚠️ Security1 sessione in stop");
            security1_session_active = false;
            security1_encryption_enabled = false;
            
            // Fallback to legacy mode if configured
            if (security1_config.fallback_to_legacy) {
                ESP_LOGI(TAG, "🔄 Fallback a modalità legacy");
                mqtt_state = MQTT_UP;
            } else {
                mqtt_state = MQTT_DOWN;
            }
            break;
            
        case SECURITY1_STATE_ERROR:
            ESP_LOGE(TAG, "❌ Security1 errore critico");
            mqtt_state = MQTT_DOWN;
            security1_session_active = false;
            security1_encryption_enabled = false;
            break;
            
        default:
            ESP_LOGD(TAG, "🔄 Security1 state non gestito: %d", state);
            break;
    }
}

/**
 * @brief Setup topic names per dual-topic architecture
 */
static esp_err_t transport_mqtt_setup_dual_topics(void)
{
    // Handshake topics: {prefix}/handshake/request + {prefix}/handshake/response (aligned with Python)
    snprintf(handshake_rx_topic, sizeof(handshake_rx_topic), 
             "%s/handshake/request", security1_config.topic_prefix);
    snprintf(handshake_tx_topic, sizeof(handshake_tx_topic), 
             "%s/handshake/response", security1_config.topic_prefix);
    
    // Operational topics: {prefix}/data/request + {prefix}/data/response (aligned with Python)
    snprintf(operational_rx_topic, sizeof(operational_rx_topic), 
             "%s/data/request", security1_config.topic_prefix);
    snprintf(operational_tx_topic, sizeof(operational_tx_topic), 
             "%s/data/response", security1_config.topic_prefix);
    
    ESP_LOGI(TAG, "📋 Topic handshake: %s / %s", handshake_rx_topic, handshake_tx_topic);
    ESP_LOGI(TAG, "📋 Topic operativi: %s / %s", operational_rx_topic, operational_tx_topic);
    
    return ESP_OK;
}

/**
 * @brief Subscribe ai topic handshake per Security1
 */
static esp_err_t transport_mqtt_subscribe_handshake_topics(void)
{
    if (!mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, handshake_rx_topic, security1_config.qos_level);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "❌ Failed to subscribe to handshake topic");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "📋 Subscribed to handshake topic: %s (msg_id=%d)", handshake_rx_topic, msg_id);
    handshake_topics_active = true;
    return ESP_OK;
}

/**
 * @brief Subscribe ai topic operativi per comunicazione crittografata
 */
static esp_err_t transport_mqtt_subscribe_operational_topics(void)
{
    if (!mqtt_client) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, operational_rx_topic, security1_config.qos_level);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "❌ Failed to subscribe to operational topic");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "📋 Subscribed to operational topic: %s (msg_id=%d)", operational_rx_topic, msg_id);
    operational_topics_active = true;
    return ESP_OK;
}

/* ---------- Back-off reconnection helpers ---------- */

/**
 * @brief Schedula un tentativo di riconnessione con back-off exponential + jitter
 */
static void schedule_reconnect(void)
{
    if (esp_timer_is_active(reconnect_timer)) {
        ESP_LOGD(TAG, "⏰ Timer riconnessione già attivo");
        return;  // Timer già schedulato
    }
    
    // Jitter ±10% per evitare thundering herd
    uint32_t jitter = esp_random() % (backoff_delay_ms / 10);
    uint32_t total_delay = backoff_delay_ms + jitter;
    
    ESP_LOGW(TAG, "🔄 Re-connect in %" PRIu32 " ms (backoff: %" PRIu32 " + jitter: %" PRIu32 ")", 
             total_delay, backoff_delay_ms, jitter);
    
    // Avvia timer one-shot
    esp_err_t err = esp_timer_start_once(reconnect_timer, total_delay * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Errore avvio timer riconnessione: %s", esp_err_to_name(err));
        return;
    }
    
    // Raddoppia back-off per prossimo tentativo (con limite massimo)
    backoff_delay_ms = (backoff_delay_ms * 2 > CONFIG_MQTT_BACKOFF_MAX_MS) 
                       ? CONFIG_MQTT_BACKOFF_MAX_MS 
                       : backoff_delay_ms * 2;
}

/**
 * @brief Callback timer per tentativo riconnessione
 */
static void reconnect_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "🔄 Tentativo riconnessione MQTT...");
    
    esp_err_t err = esp_mqtt_client_reconnect(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Riconnessione fallita: %s", esp_err_to_name(err));
        // Il fallimento triggererà MQTT_EVENT_ERROR che ri-schedulerà
    }
}

/**
 * @brief Task per inviare risposte MQTT dal respQueue
 */
static void mqtt_tx_task(void *arg)
{
    resp_frame_t resp;
    
    ESP_LOGI(TAG, "🚀 MQTT TX task avviato");
    
    for (;;) {
        if (xQueueReceive(resp_queue, &resp, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "📤 Ricevuta risposta per origin %d", resp.origin);
            
            // Invia solo risposte con origin MQTT
            if (resp.origin != ORIGIN_MQTT) {
                ESP_LOGV(TAG, "⏭️ Risposta non per MQTT, saltando");
                continue;
            }
            
            // Controlla stato MQTT prima di inviare
            if (mqtt_state != MQTT_UP) {
                ESP_LOGW(TAG, "⚠️ MQTT down - scartando risposta id=%u", resp.id);
                if (resp.is_final && resp.payload) {
                    free(resp.payload);
                }
                continue;
            }
            
            // Encode della risposta JSON per MQTT
            char *json_response = encode_json_response(&resp);
            
            if (json_response) {
                size_t json_len = strlen(json_response);
                
                // Pubblica sul topic di risposta
                int msg_id = esp_mqtt_client_publish(
                    mqtt_client,
                    CONFIG_MQTT_RESP_TOPIC,
                    json_response,
                    (int)json_len,
                    CONFIG_MQTT_QOS_LEVEL,
                    false
                );
                
                if (msg_id >= 0) {
                    ESP_LOGI(TAG, "✅ Risposta MQTT JSON pubblicata (msg_id=%d, len=%zu)", msg_id, json_len);
                } else {
                    ESP_LOGE(TAG, "❌ Errore pubblicazione risposta MQTT");
                }
                
                free(json_response);
            } else {
                ESP_LOGE(TAG, "❌ Errore encoding risposta JSON");
            }
            
            // Cleanup payload se finale
            if (resp.is_final && resp.payload) {
                free(resp.payload);
            }
        }
    }
}

/**
 * @brief Handler eventi MQTT
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ MQTT_CONNECTED - Broker raggiunto");
            
            // Reset back-off su connessione riuscita
            backoff_delay_ms = CONFIG_MQTT_BACKOFF_INITIAL_MS;
            
            // Ferma timer riconnessione se attivo
            if (esp_timer_is_active(reconnect_timer)) {
                esp_timer_stop(reconnect_timer);
                ESP_LOGD(TAG, "⏰ Timer riconnessione fermato");
            }
            
            // Check if Security1 session is active
            if (security1_session_active || strlen(handshake_rx_topic) > 0) {
                ESP_LOGI(TAG, "🔐 Security1 mode detected - subscribing to handshake topics");
                mqtt_state = MQTT_CONNECTING;  // Stay in connecting until handshake
                
                // Subscribe to handshake topics for Security1
                esp_err_t ret = transport_mqtt_subscribe_handshake_topics();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "❌ Failed to subscribe to Security1 handshake topics");
                    mqtt_state = MQTT_DOWN;
                } else {
                    mqtt_state = MQTT_SECURITY1_HANDSHAKE;
                }
            } else {
                ESP_LOGI(TAG, "📋 Legacy mode - subscribing to command topic");
                mqtt_state = MQTT_UP;
                
                // Subscribe al topic dei comandi (legacy mode)
                int msg_id = esp_mqtt_client_subscribe(mqtt_client, CONFIG_MQTT_CMD_TOPIC, CONFIG_MQTT_QOS_LEVEL);
                ESP_LOGI(TAG, "📋 Subscribed a %s (msg_id=%d)", CONFIG_MQTT_CMD_TOPIC, msg_id);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "❌ MQTT_DISCONNECTED - Connessione persa");
            mqtt_state = MQTT_DOWN;
            schedule_reconnect();
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "❌ MQTT_ERROR - Errore di connessione");
            mqtt_state = MQTT_DOWN;
            schedule_reconnect();
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "✅ Subscription confermata (msg_id=%d)", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "❌ Unsubscription confermata (msg_id=%d)", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "📤 Messaggio pubblicato (msg_id=%d)", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "📨 Messaggio ricevuto su topic %.*s", event->topic_len, event->topic);
            
            // Gestione dual-topic Security1
            // Check handshake topic FIRST (even if session not active yet)
            if (handshake_topics_active && 
                strlen(handshake_rx_topic) == event->topic_len &&
                strncmp(event->topic, handshake_rx_topic, event->topic_len) == 0) {
                ESP_LOGI(TAG, "🤝 Handshake message received");
                
                // Handshake messages handled internally by Security1 session
                ESP_LOGI(TAG, "🔄 Handshake message received (%d bytes)", event->data_len);
                
                // Forward handshake message to Security1 protocomm
                esp_err_t forward_ret = security1_process_handshake_message(
                    (const uint8_t*)event->data, 
                    (size_t)event->data_len,
                    handshake_tx_topic
                );
                
                if (forward_ret != ESP_OK) {
                    ESP_LOGE(TAG, "❌ Failed to forward handshake message to Security1: %s", 
                             esp_err_to_name(forward_ret));
                } else {
                    ESP_LOGI(TAG, "✅ Handshake message forwarded to Security1 successfully");
                }
                return;
            }
            
            // Check operational topics only if session is active
            if (security1_session_active) {
                // Check if message is on operational topic (ensure exact match)
                if (operational_topics_active && 
                    strlen(operational_rx_topic) == event->topic_len &&
                    strncmp(event->topic, operational_rx_topic, event->topic_len) == 0) {
                    ESP_LOGI(TAG, "🔒 Encrypted message received");
                    
                    // Decrypt message using Security1 session
                    security1_buffer_t ciphertext = {
                        .data = (uint8_t*)event->data,
                        .length = (size_t)event->data_len
                    };
                    
                    // Allocate buffer for decrypted data
                    size_t decrypted_size = security1_get_decrypted_size(ciphertext.length);
                    if (decrypted_size == 0) {
                        ESP_LOGE(TAG, "❌ Invalid ciphertext length for decryption");
                        return;
                    }
                    
                    security1_buffer_t plaintext = {
                        .data = malloc(decrypted_size),
                        .length = decrypted_size
                    };
                    
                    if (!plaintext.data) {
                        ESP_LOGE(TAG, "❌ Failed to allocate decryption buffer");
                        return;
                    }
                    
                    esp_err_t ret = security1_decrypt(&ciphertext, &plaintext);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "❌ Failed to decrypt message");
                        return;
                    }
                    
                    // Process decrypted command
                    cmd_frame_t cmd;
                    if (decode_json_command((char*)plaintext.data, plaintext.length, &cmd)) {
                        cmd.origin = ORIGIN_MQTT;
                        
                        if (xQueueSend(cmd_queue, &cmd, 0) == pdTRUE) {
                            ESP_LOGI(TAG, "✅ Encrypted command processed (id=%u, op=%s)", cmd.id, cmd.op);
                        } else {
                            ESP_LOGW(TAG, "⚠️ Command queue full, encrypted command lost");
                            if (cmd.payload) {
                                free(cmd.payload);
                            }
                        }
                    } else {
                        ESP_LOGE(TAG, "❌ Failed to decode encrypted command");
                    }
                    
                    // Free decrypted buffer
                    if (plaintext.data) {
                        free(plaintext.data);
                    }
                    return;
                }
            }
            
            // Legacy mode: check regular command topic (ensure exact match)
            if (strlen(CONFIG_MQTT_CMD_TOPIC) == event->topic_len &&
                strncmp(event->topic, CONFIG_MQTT_CMD_TOPIC, event->topic_len) == 0) {
                ESP_LOGI(TAG, "📋 Legacy command received");
                
                cmd_frame_t cmd;
                if (decode_json_command((char*)event->data, event->data_len, &cmd)) {
                    cmd.origin = ORIGIN_MQTT;
                    
                    if (xQueueSend(cmd_queue, &cmd, 0) == pdTRUE) {
                        ESP_LOGI(TAG, "✅ Legacy command processed (id=%u, op=%s)", cmd.id, cmd.op);
                    } else {
                        ESP_LOGW(TAG, "⚠️ Command queue full, legacy command lost");
                        if (cmd.payload) {
                            free(cmd.payload);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "❌ Failed to decode legacy command");
                }
            } else {
                ESP_LOGW(TAG, "⚠️ Message on unknown topic: %.*s", event->topic_len, event->topic);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "🔄 Evento MQTT: %" PRId32, event_id);
            break;
    }
}

void transport_mqtt_init(QueueHandle_t cmdQueue, QueueHandle_t respQueue)
{
    ESP_LOGI(TAG, "🏗️ Inizializzazione transport MQTT");
    
    if (!cmdQueue || !respQueue) {
        ESP_LOGE(TAG, "❌ Queue non valide");
        return;
    }
    
    cmd_queue = cmdQueue;
    resp_queue = respQueue;
    
    // Registra componente nel framework error management unificato
    esp_err_t err = error_manager_register_component(
        ERROR_COMPONENT_MQTT_TRANSPORT,
        NULL,  // Usa configurazione default
        NULL,  // Nessun callback recovery personalizzato per ora
        NULL   // Nessun user data
    );
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to register with unified error manager: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "🎯 MQTT transport registered with unified error manager");
    }
    
    // Configurazione client MQTT
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_MQTT_BROKER_URI,
        },
        .network = {
            .timeout_ms = 5000,
        },
        .session = {
            .keepalive = CONFIG_MQTT_KEEPALIVE_INTERVAL,
        },
        .credentials = {
            .client_id = "smartdrip_esp32",
        }
    };
    
    // Crea client MQTT
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "❌ Errore creazione client MQTT");
        return;
    }
    
    // Registra handler eventi
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Crea timer per riconnessione con back-off
    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_callback,
        .name = "mqtt_reconn"
    };
    esp_err_t timer_err = esp_timer_create(&timer_args, &reconnect_timer);
    if (timer_err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Errore creazione timer riconnessione: %s", esp_err_to_name(timer_err));
        return;
    }
    
    ESP_LOGI(TAG, "✅ Transport MQTT inizializzato");
    ESP_LOGI(TAG, "🌐 Broker: %s", CONFIG_MQTT_BROKER_URI);
    ESP_LOGI(TAG, "📋 Topic CMD: %s", CONFIG_MQTT_CMD_TOPIC);
    ESP_LOGI(TAG, "📤 Topic RESP: %s", CONFIG_MQTT_RESP_TOPIC);
    ESP_LOGI(TAG, "⚙️ Back-off: %" PRIu32 "-%" PRIu32 " ms", 
             (uint32_t)CONFIG_MQTT_BACKOFF_INITIAL_MS, (uint32_t)CONFIG_MQTT_BACKOFF_MAX_MS);
}

void transport_mqtt_start(void)
{
    ESP_LOGI(TAG, "🚀 Avvio transport MQTT");
    
    if (!mqtt_client) {
        ESP_LOGE(TAG, "❌ Client MQTT non inizializzato");
        return;
    }
    
    // Avvia client MQTT
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Errore avvio client MQTT: %s", esp_err_to_name(err));
        return;
    }
    
    // Crea task TX per risposte
    BaseType_t result = xTaskCreate(
        mqtt_tx_task,
        "MQTT_TX",
        4096,
        NULL,
        5,
        &tx_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Errore creazione task TX");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Transport MQTT avviato");
}

void transport_mqtt_stop(void)
{
    ESP_LOGI(TAG, "🛑 Arresto transport MQTT");
    
    // Ferma timer riconnessione
    if (reconnect_timer && esp_timer_is_active(reconnect_timer)) {
        esp_timer_stop(reconnect_timer);
    }
    
    if (tx_task_handle) {
        vTaskDelete(tx_task_handle);
        tx_task_handle = NULL;
    }
    
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        mqtt_state = MQTT_DOWN;
    }
    
    ESP_LOGI(TAG, "✅ Transport MQTT arrestato");
}

bool transport_mqtt_is_connected(void)
{
    return (mqtt_state == MQTT_UP);
}

mqtt_state_t transport_mqtt_get_state(void)
{
    return mqtt_state;
}

void transport_mqtt_cleanup(void)
{
    ESP_LOGI(TAG, "🧹 Cleanup transport MQTT");
    
    transport_mqtt_stop();
    
    // Cleanup timer riconnessione
    if (reconnect_timer) {
        esp_timer_delete(reconnect_timer);
        reconnect_timer = NULL;
    }
    
    if (mqtt_client) {
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    // Reset stato
    cmd_queue = NULL;
    resp_queue = NULL;
    mqtt_state = MQTT_DOWN;
    backoff_delay_ms = CONFIG_MQTT_BACKOFF_INITIAL_MS;
    
    ESP_LOGI(TAG, "✅ Cleanup transport MQTT completato");
}

/* ────────────── Security1 API Implementation ────────────── */

esp_err_t transport_mqtt_start_with_security1(QueueHandle_t cmdQueue, 
                                             QueueHandle_t respQueue,
                                             const transport_mqtt_security1_config_t *sec1_config)
{
    if (!cmdQueue || !respQueue || !sec1_config) {
        ESP_LOGE(TAG, "❌ Invalid parameters for Security1 MQTT");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔐 Starting MQTT transport with Security1 support");
    
    // Store Security1 configuration
    memcpy(&security1_config, sec1_config, sizeof(transport_mqtt_security1_config_t));
    
    // Initialize regular MQTT transport first
    transport_mqtt_init(cmdQueue, respQueue);
    
    // Setup dual-topic architecture
    esp_err_t ret = transport_mqtt_setup_dual_topics();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to setup dual-topic architecture");
        return ret;
    }
    
    // Configure MQTT client with Security1 broker URI
    if (mqtt_client) {
        esp_mqtt_client_destroy(mqtt_client);
    }
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = security1_config.broker_uri,
        },
        .network = {
            .timeout_ms = 5000,
        },
        .session = {
            .keepalive = security1_config.keepalive_interval,
        },
        .credentials = {
            .client_id = security1_config.client_id,
        }
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "❌ Failed to create Security1 MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Start Security1 session with MQTT handshake
    security1_handshake_config_t handshake_config = {
        .mqtt = {
            .broker_uri = {0},
            .topic_prefix = {0},
            .qos_level = security1_config.qos_level,
            .client_id = {0},
            .keepalive_interval = security1_config.keepalive_interval
        }
    };
    
    // Copy configuration strings
    strncpy(handshake_config.mqtt.broker_uri, security1_config.broker_uri, 
            sizeof(handshake_config.mqtt.broker_uri) - 1);
    strncpy(handshake_config.mqtt.topic_prefix, security1_config.topic_prefix, 
            sizeof(handshake_config.mqtt.topic_prefix) - 1);
    strncpy(handshake_config.mqtt.client_id, security1_config.client_id, 
            sizeof(handshake_config.mqtt.client_id) - 1);
    
    ret = security1_session_start(
        SECURITY1_HANDSHAKE_MQTT,
        &handshake_config,
        security1_config.proof_of_possession,
        transport_mqtt_security1_event_callback,
        NULL
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start Security1 session: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return ret;
    }
    
    // Start MQTT client
    ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start Security1 MQTT client: %s", esp_err_to_name(ret));
        security1_session_stop();
        return ret;
    }
    
    // Create TX task
    BaseType_t result = xTaskCreate(
        mqtt_tx_task,
        "MQTT_TX_SEC1",
        4096,
        NULL,
        5,
        &tx_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create Security1 TX task");
        esp_mqtt_client_stop(mqtt_client);
        security1_session_stop();
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ MQTT transport with Security1 started successfully");
    ESP_LOGI(TAG, "🌐 Broker: %s", security1_config.broker_uri);
    ESP_LOGI(TAG, "🔑 PoP: %s", security1_config.proof_of_possession);
    ESP_LOGI(TAG, "📋 Topic prefix: %s", security1_config.topic_prefix);
    
    return ESP_OK;
}

esp_err_t transport_mqtt_send_encrypted(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!security1_session_active || !security1_encryption_enabled) {
        ESP_LOGE(TAG, "❌ Security1 session not active for encrypted send");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!operational_topics_active) {
        ESP_LOGE(TAG, "❌ Operational topics not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Encrypt data using Security1 session
    security1_buffer_t plaintext = {
        .data = (uint8_t*)data,
        .length = len
    };
    
    security1_buffer_t ciphertext = {0};
    
    esp_err_t ret = security1_encrypt(&plaintext, &ciphertext);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to encrypt data: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Publish encrypted data to operational topic
    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        operational_tx_topic,
        (char*)ciphertext.data,
        (int)ciphertext.length,
        security1_config.qos_level,
        false
    );
    
    // Free encrypted buffer
    if (ciphertext.data) {
        free(ciphertext.data);
    }
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "❌ Failed to publish encrypted data");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "🔒 Encrypted data published (msg_id=%d, len=%zu→%zu)", 
             msg_id, len, ciphertext.length);
    
    return ESP_OK;
}

bool transport_mqtt_is_security1_active(void)
{
    return security1_session_active && security1_encryption_enabled;
}

esp_err_t transport_mqtt_get_security1_info(bool *session_established,
                                           bool *encryption_active, 
                                           bool *handshake_topics_active_out,
                                           bool *operational_topics_active_out)
{
    if (!session_established || !encryption_active || 
        !handshake_topics_active_out || !operational_topics_active_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *session_established = security1_session_active;
    *encryption_active = security1_encryption_enabled;
    *handshake_topics_active_out = handshake_topics_active;
    *operational_topics_active_out = operational_topics_active;
    
    return ESP_OK;
}

esp_err_t transport_mqtt_transition_to_operational(void)
{
    ESP_LOGI(TAG, "🔄 Transitioning to operational topics");
    
    if (!mqtt_client) {
        ESP_LOGE(TAG, "❌ MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Subscribe to operational topics
    esp_err_t ret = transport_mqtt_subscribe_operational_topics();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to subscribe to operational topics");
        return ret;
    }
    
    // Unsubscribe from handshake topics (optional cleanup)
    esp_mqtt_client_unsubscribe(mqtt_client, handshake_rx_topic);
    handshake_topics_active = false;
    
    ESP_LOGI(TAG, "✅ Successfully transitioned to operational mode");
    ESP_LOGI(TAG, "📋 Active topic: %s", operational_rx_topic);
    
    return ESP_OK;
}

esp_err_t transport_mqtt_publish_handshake_response(const char *topic, 
                                                   const uint8_t *data, 
                                                   size_t data_len)
{
    if (!topic || !data || data_len == 0) {
        ESP_LOGE(TAG, "❌ Invalid parameters for handshake response");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!mqtt_client) {
        ESP_LOGE(TAG, "❌ MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "📡 Publishing handshake response to %s (%zu bytes)", topic, data_len);
    
    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topic,
        (const char*)data,
        (int)data_len,
        1, // QoS 1 for handshake reliability
        false // No retain
    );
    
    if (msg_id < 0) {
        ESP_LOGE(TAG, "❌ Failed to publish handshake response");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ Handshake response published (msg_id=%d)", msg_id);
    return ESP_OK;
}