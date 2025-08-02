#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "transport_mqtt.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "TRANSPORT_MQTT_TEST";

static QueueHandle_t test_cmd_queue = NULL;
static QueueHandle_t test_resp_queue = NULL;

void setUp(void)
{
    // Crea code per ogni test
    if (!test_cmd_queue) {
        test_cmd_queue = xQueueCreate(5, sizeof(cmd_frame_t));
    }
    if (!test_resp_queue) {
        test_resp_queue = xQueueCreate(5, sizeof(resp_frame_t));
    }
}

void tearDown(void)
{
    // Cleanup code dopo ogni test
    if (test_cmd_queue) {
        xQueueReset(test_cmd_queue);
    }
    if (test_resp_queue) {
        xQueueReset(test_resp_queue);
    }
}

TEST_CASE("transport_mqtt: queue creation and validation", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test creazione e validazione queue MQTT");
    
    TEST_ASSERT_NOT_NULL(test_cmd_queue);
    TEST_ASSERT_NOT_NULL(test_resp_queue);
    
    // Verifica che le queue siano vuote
    UBaseType_t cmd_waiting = uxQueueMessagesWaiting(test_cmd_queue);
    UBaseType_t resp_waiting = uxQueueMessagesWaiting(test_resp_queue);
    
    TEST_ASSERT_EQUAL(0, cmd_waiting);
    TEST_ASSERT_EQUAL(0, resp_waiting);
}

TEST_CASE("transport_mqtt: initialization with valid queues", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test inizializzazione transport MQTT con queue valide");
    
    // Test inizializzazione (non avvia connessione)
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    
    // Verifica stato iniziale
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    ESP_LOGI(TAG, "✅ Inizializzazione completata senza errori");
}

TEST_CASE("transport_mqtt: initialization with null queues", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test inizializzazione transport MQTT con queue NULL");
    
    // Test con queue NULL (dovrebbe gestire gracefully)
    transport_mqtt_init(NULL, test_resp_queue);
    transport_mqtt_init(test_cmd_queue, NULL);
    transport_mqtt_init(NULL, NULL);
    
    // Se arriva qui, non è crashato
    TEST_PASS();
}

TEST_CASE("transport_mqtt: connection state management", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test gestione stato connessione MQTT");
    
    // Stato iniziale
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    // Inizializza transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    
    // Ancora disconnesso
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    // Nota: Non testiamo connessione reale perché richiede broker MQTT
    ESP_LOGI(TAG, "✅ Gestione stato funzionante");
}

TEST_CASE("transport_mqtt: queue operations simulation", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test operazioni queue simulate");
    
    // Simula comando MQTT in arrivo
    cmd_frame_t test_cmd = {
        .id = 42,
        .origin = ORIGIN_MQTT,
        .op = "test_op",
        .len = 0,
        .payload = NULL
    };
    
    // Invia comando alla queue
    BaseType_t result = xQueueSend(test_cmd_queue, &test_cmd, 0);
    TEST_ASSERT_EQUAL(pdTRUE, result);
    
    // Verifica che sia in queue
    UBaseType_t waiting = uxQueueMessagesWaiting(test_cmd_queue);
    TEST_ASSERT_EQUAL(1, waiting);
    
    // Ricevi comando dalla queue
    cmd_frame_t received_cmd;
    result = xQueueReceive(test_cmd_queue, &received_cmd, 0);
    TEST_ASSERT_EQUAL(pdTRUE, result);
    
    // Verifica contenuto
    TEST_ASSERT_EQUAL(42, received_cmd.id);
    TEST_ASSERT_EQUAL(ORIGIN_MQTT, received_cmd.origin);
    TEST_ASSERT_EQUAL_STRING("test_op", received_cmd.op);
    
    ESP_LOGI(TAG, "✅ Operazioni queue simulate con successo");
}

TEST_CASE("transport_mqtt: response queue operations", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test operazioni queue risposte");
    
    // Simula risposta per MQTT
    resp_frame_t test_resp = {
        .id = 42,
        .origin = ORIGIN_MQTT,
        .status = 0,
        .len = 0,
        .payload = NULL,
        .is_final = true
    };
    
    // Invia risposta alla queue
    BaseType_t result = xQueueSend(test_resp_queue, &test_resp, 0);
    TEST_ASSERT_EQUAL(pdTRUE, result);
    
    // Ricevi risposta dalla queue
    resp_frame_t received_resp;
    result = xQueueReceive(test_resp_queue, &received_resp, 0);
    TEST_ASSERT_EQUAL(pdTRUE, result);
    
    // Verifica contenuto
    TEST_ASSERT_EQUAL(42, received_resp.id);
    TEST_ASSERT_EQUAL(ORIGIN_MQTT, received_resp.origin);
    TEST_ASSERT_EQUAL(0, received_resp.status);
    TEST_ASSERT_TRUE(received_resp.is_final);
    
    ESP_LOGI(TAG, "✅ Queue risposte funzionante");
}

TEST_CASE("transport_mqtt: cleanup operations", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test operazioni cleanup");
    
    // Inizializza
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    
    // Stop (dovrebbe essere safe anche se non avviato)
    transport_mqtt_stop();
    
    // Cleanup
    transport_mqtt_cleanup();
    
    // Verifica stato finale
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    ESP_LOGI(TAG, "✅ Cleanup completato senza errori");
}

static void init_system(void)
{
    // Inizializza NVS (richiesto per WiFi/MQTT)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Inizializza network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test transport_mqtt component");
    
    // Inizializza sistema per test MQTT
    init_system();
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    // Cleanup finale
    if (test_cmd_queue) {
        vQueueDelete(test_cmd_queue);
    }
    if (test_resp_queue) {
        vQueueDelete(test_resp_queue);
    }
    
    ESP_LOGI(TAG, "✅ Test transport_mqtt completati");
}