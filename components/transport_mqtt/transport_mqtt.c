#include "transport_mqtt.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "codec.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "TRANSPORT_MQTT";

// Client MQTT e configurazione
static esp_mqtt_client_handle_t mqtt_client = NULL;
static QueueHandle_t cmd_queue = NULL;
static QueueHandle_t resp_queue = NULL;
static bool is_connected = false;

// Task handle per il task TX
static TaskHandle_t tx_task_handle = NULL;

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
            
            if (!is_connected) {
                ESP_LOGW(TAG, "⚠️ MQTT non connesso, risposta persa");
                if (resp.is_final && resp.payload) {
                    free(resp.payload);
                }
                continue;
            }
            
            // Encode della risposta (usa codec BLE per ora)
            size_t encoded_len;
            uint8_t *encoded_data = encode_ble_resp(&resp, &encoded_len);
            
            if (encoded_data) {
                // Pubblica sul topic di risposta
                int msg_id = esp_mqtt_client_publish(
                    mqtt_client,
                    CONFIG_MQTT_RESP_TOPIC,
                    (char*)encoded_data,
                    (int)encoded_len,
                    CONFIG_MQTT_QOS_LEVEL,
                    false
                );
                
                if (msg_id >= 0) {
                    ESP_LOGI(TAG, "✅ Risposta MQTT pubblicata (msg_id=%d, len=%zu)", msg_id, encoded_len);
                } else {
                    ESP_LOGE(TAG, "❌ Errore pubblicazione risposta MQTT");
                }
                
                free(encoded_data);
            } else {
                ESP_LOGE(TAG, "❌ Errore encoding risposta");
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
            ESP_LOGI(TAG, "🔗 MQTT connesso al broker");
            is_connected = true;
            
            // Subscribe al topic dei comandi
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, CONFIG_MQTT_CMD_TOPIC, CONFIG_MQTT_QOS_LEVEL);
            ESP_LOGI(TAG, "📋 Subscribed a %s (msg_id=%d)", CONFIG_MQTT_CMD_TOPIC, msg_id);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "🔌 MQTT disconnesso");
            is_connected = false;
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
                // Decode del comando (usa codec BLE per ora)
                cmd_frame_t cmd;
                if (decode_ble_frame((uint8_t*)event->data, event->data_len, &cmd)) {
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
                    ESP_LOGE(TAG, "❌ Errore decode comando MQTT");
                }
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ Errore MQTT: %s", strerror(event->error_handle->esp_transport_sock_errno));
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
    
    ESP_LOGI(TAG, "✅ Transport MQTT inizializzato");
    ESP_LOGI(TAG, "🌐 Broker: %s", CONFIG_MQTT_BROKER_URI);
    ESP_LOGI(TAG, "📋 Topic CMD: %s", CONFIG_MQTT_CMD_TOPIC);
    ESP_LOGI(TAG, "📤 Topic RESP: %s", CONFIG_MQTT_RESP_TOPIC);
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
    
    if (tx_task_handle) {
        vTaskDelete(tx_task_handle);
        tx_task_handle = NULL;
    }
    
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        is_connected = false;
    }
    
    ESP_LOGI(TAG, "✅ Transport MQTT arrestato");
}

bool transport_mqtt_is_connected(void)
{
    return is_connected;
}

void transport_mqtt_cleanup(void)
{
    ESP_LOGI(TAG, "🧹 Cleanup transport MQTT");
    
    transport_mqtt_stop();
    
    if (mqtt_client) {
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    cmd_queue = NULL;
    resp_queue = NULL;
    is_connected = false;
    
    ESP_LOGI(TAG, "✅ Cleanup transport MQTT completato");
}