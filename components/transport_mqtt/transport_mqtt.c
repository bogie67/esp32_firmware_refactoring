#include "transport_mqtt.h"
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
            mqtt_state = MQTT_UP;
            
            // Reset back-off su connessione riuscita
            backoff_delay_ms = CONFIG_MQTT_BACKOFF_INITIAL_MS;
            
            // Ferma timer riconnessione se attivo
            if (esp_timer_is_active(reconnect_timer)) {
                esp_timer_stop(reconnect_timer);
                ESP_LOGD(TAG, "⏰ Timer riconnessione fermato");
            }
            
            // Subscribe al topic dei comandi
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, CONFIG_MQTT_CMD_TOPIC, CONFIG_MQTT_QOS_LEVEL);
            ESP_LOGI(TAG, "📋 Subscribed a %s (msg_id=%d)", CONFIG_MQTT_CMD_TOPIC, msg_id);
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
            
            // Verifica che sia sul topic comandi
            if (strncmp(event->topic, CONFIG_MQTT_CMD_TOPIC, event->topic_len) == 0) {
                // Decode del comando JSON per MQTT
                cmd_frame_t cmd;
                if (decode_json_command((char*)event->data, event->data_len, &cmd)) {
                    // Imposta origin MQTT
                    cmd.origin = ORIGIN_MQTT;
                    
                    // Invia alla command queue
                    if (xQueueSend(cmd_queue, &cmd, 0) == pdTRUE) {
                        ESP_LOGI(TAG, "✅ Comando MQTT inoltrato (id=%u, op=%s)", cmd.id, cmd.op);
                    } else {
                        ESP_LOGW(TAG, "⚠️ Command queue piena, comando perso");
                        // Cleanup payload se allocato
                        if (cmd.payload) {
                            free(cmd.payload);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "❌ Errore decode comando MQTT JSON");
                }
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
    esp_err_t err = esp_timer_create(&timer_args, &reconnect_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Errore creazione timer riconnessione: %s", esp_err_to_name(err));
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