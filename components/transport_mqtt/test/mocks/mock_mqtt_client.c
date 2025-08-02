#include "mock_mqtt_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MOCK_MQTT";

// Mock state
static bool mock_client_started = false;
static bool mock_client_connected = false;
static esp_mqtt_event_handler_t mock_event_handler = NULL;
static void *mock_handler_args = NULL;
static char mock_last_published_topic[128] = {0};
static char mock_last_published_data[256] = {0};
static int mock_last_published_len = 0;

// Reset mock state
void mock_mqtt_reset(void)
{
    mock_client_started = false;
    mock_client_connected = false;
    mock_event_handler = NULL;
    mock_handler_args = NULL;
    memset(mock_last_published_topic, 0, sizeof(mock_last_published_topic));
    memset(mock_last_published_data, 0, sizeof(mock_last_published_data));
    mock_last_published_len = 0;
}

// Mock implementation of esp_mqtt_client_init
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT client init");
    // Return a fake handle (non-NULL)
    return (esp_mqtt_client_handle_t)0x12345678;
}

// Mock implementation of esp_mqtt_client_register_event
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client,
                                        esp_mqtt_event_id_t event,
                                        esp_mqtt_event_handler_t event_handler,
                                        void *event_handler_arg)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT register event handler");
    mock_event_handler = event_handler;
    mock_handler_args = event_handler_arg;
    return ESP_OK;
}

// Mock implementation of esp_mqtt_client_start
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT client start");
    mock_client_started = true;
    return ESP_OK;
}

// Mock implementation of esp_mqtt_client_stop
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT client stop");
    mock_client_started = false;
    mock_client_connected = false;
    return ESP_OK;
}

// Mock implementation of esp_mqtt_client_destroy
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT client destroy");
    mock_client_started = false;
    mock_client_connected = false;
    return ESP_OK;
}

// Mock implementation of esp_mqtt_client_publish
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client,
                           const char *topic,
                           const char *data,
                           int len,
                           int qos,
                           int retain)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT publish to %s", topic);
    
    // Store last published message for verification
    strncpy(mock_last_published_topic, topic, sizeof(mock_last_published_topic) - 1);
    
    if (len == 0) {
        len = strlen(data);
    }
    
    if (len < sizeof(mock_last_published_data)) {
        memcpy(mock_last_published_data, data, len);
        mock_last_published_data[len] = '\0';
        mock_last_published_len = len;
    }
    
    // Return a fake message ID
    return 12345;
}

// Mock implementation of esp_mqtt_client_subscribe
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                             const char *topic,
                             int qos)
{
    ESP_LOGI(TAG, "🎭 Mock MQTT subscribe to %s", topic);
    // Return a fake message ID
    return 54321;
}

// Helper functions to simulate MQTT events
void mock_mqtt_simulate_connected(void)
{
    if (!mock_event_handler) {
        ESP_LOGW(TAG, "⚠️ No event handler registered");
        return;
    }
    
    ESP_LOGI(TAG, "🎭 Simulating MQTT connected event");
    mock_client_connected = true;
    
    esp_mqtt_event_t event = {
        .event_id = MQTT_EVENT_CONNECTED,
        .client = (esp_mqtt_client_handle_t)0x12345678,
        .msg_id = 0
    };
    
    mock_event_handler(mock_handler_args, NULL, MQTT_EVENT_CONNECTED, &event);
}

void mock_mqtt_simulate_disconnected(void)
{
    if (!mock_event_handler) {
        ESP_LOGW(TAG, "⚠️ No event handler registered");
        return;
    }
    
    ESP_LOGI(TAG, "🎭 Simulating MQTT disconnected event");
    mock_client_connected = false;
    
    esp_mqtt_event_t event = {
        .event_id = MQTT_EVENT_DISCONNECTED,
        .client = (esp_mqtt_client_handle_t)0x12345678,
        .msg_id = 0
    };
    
    mock_event_handler(mock_handler_args, NULL, MQTT_EVENT_DISCONNECTED, &event);
}

void mock_mqtt_simulate_data(const char *topic, const char *data, int data_len)
{
    if (!mock_event_handler) {
        ESP_LOGW(TAG, "⚠️ No event handler registered");
        return;
    }
    
    ESP_LOGI(TAG, "🎭 Simulating MQTT data on topic %s", topic);
    
    esp_mqtt_event_t event = {
        .event_id = MQTT_EVENT_DATA,
        .client = (esp_mqtt_client_handle_t)0x12345678,
        .topic = (char*)topic,
        .topic_len = strlen(topic),
        .data = (char*)data,
        .data_len = data_len,
        .msg_id = 99999
    };
    
    mock_event_handler(mock_handler_args, NULL, MQTT_EVENT_DATA, &event);
}

// Mock state getters for test verification
bool mock_mqtt_is_started(void)
{
    return mock_client_started;
}

bool mock_mqtt_is_connected(void)
{
    return mock_client_connected;
}

const char* mock_mqtt_get_last_published_topic(void)
{
    return mock_last_published_topic;
}

const char* mock_mqtt_get_last_published_data(void)
{
    return mock_last_published_data;
}

int mock_mqtt_get_last_published_len(void)
{
    return mock_last_published_len;
}