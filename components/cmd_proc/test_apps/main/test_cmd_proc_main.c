#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cmd_frame.h"
#include "resp_frame.h"

static const char *TAG = "CMD_PROC_TEST";

// Mock delle queue globali necessarie per cmd_proc
QueueHandle_t cmdQueue = NULL;
QueueHandle_t respQueue = NULL;

void setUp(void)
{
    // Setup before each test
    if (!cmdQueue) {
        cmdQueue = xQueueCreate(4, sizeof(cmd_frame_t));
    }
    if (!respQueue) {
        respQueue = xQueueCreate(4, sizeof(resp_frame_t));
    }
}

void tearDown(void)
{
    // Cleanup after each test - non eliminiamo le queue per evitare problemi
}

TEST_CASE("cmd_proc: queue creation", "[cmd_proc]")
{
    ESP_LOGI(TAG, "🧪 Test cmd_proc queue creation");
    
    TEST_ASSERT_NOT_NULL(cmdQueue);
    TEST_ASSERT_NOT_NULL(respQueue);
    
    // Verifica che le queue siano vuote
    UBaseType_t cmd_waiting = uxQueueMessagesWaiting(cmdQueue);
    UBaseType_t resp_waiting = uxQueueMessagesWaiting(respQueue);
    
    TEST_ASSERT_EQUAL(0, cmd_waiting);
    TEST_ASSERT_EQUAL(0, resp_waiting);
}

TEST_CASE("cmd_proc: queue operations", "[cmd_proc]")
{
    ESP_LOGI(TAG, "🧪 Test cmd_proc queue operations");
    
    // Crea un comando di test
    cmd_frame_t test_cmd = {
        .id = 123,
        .origin = 1,
        .op = "test",
        .len = 0,
        .payload = NULL
    };
    
    // Invia comando alla queue
    BaseType_t send_result = xQueueSend(cmdQueue, &test_cmd, 0);
    TEST_ASSERT_EQUAL(pdTRUE, send_result);
    
    // Verifica che il comando sia in queue
    UBaseType_t waiting = uxQueueMessagesWaiting(cmdQueue);
    TEST_ASSERT_EQUAL(1, waiting);
    
    // Ricevi il comando dalla queue
    cmd_frame_t received_cmd;
    BaseType_t receive_result = xQueueReceive(cmdQueue, &received_cmd, 0);
    TEST_ASSERT_EQUAL(pdTRUE, receive_result);
    
    // Verifica il contenuto
    TEST_ASSERT_EQUAL(123, received_cmd.id);
    TEST_ASSERT_EQUAL(1, received_cmd.origin);
    TEST_ASSERT_EQUAL_STRING("test", received_cmd.op);
}

TEST_CASE("cmd_proc: response queue operations", "[cmd_proc]")
{
    ESP_LOGI(TAG, "🧪 Test cmd_proc response queue operations");
    
    // Crea una risposta di test
    resp_frame_t test_resp = {
        .id = 456,
        .origin = 2,
        .status = 0,
        .len = 0,
        .payload = NULL,
        .is_final = true
    };
    
    // Invia risposta alla queue
    BaseType_t send_result = xQueueSend(respQueue, &test_resp, 0);
    TEST_ASSERT_EQUAL(pdTRUE, send_result);
    
    // Ricevi risposta dalla queue
    resp_frame_t received_resp;
    BaseType_t receive_result = xQueueReceive(respQueue, &received_resp, 0);
    TEST_ASSERT_EQUAL(pdTRUE, receive_result);
    
    // Verifica il contenuto
    TEST_ASSERT_EQUAL(456, received_resp.id);
    TEST_ASSERT_EQUAL(2, received_resp.origin);
    TEST_ASSERT_EQUAL(0, received_resp.status);
    TEST_ASSERT_TRUE(received_resp.is_final);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test cmd_proc component");
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test cmd_proc completati");
}