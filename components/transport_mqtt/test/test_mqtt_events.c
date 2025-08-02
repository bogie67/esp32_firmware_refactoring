#include "unity.h"
#include "transport_mqtt.h"
#include "mock_mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "TEST_MQTT_EVENTS";

static QueueHandle_t test_cmd_queue = NULL;
static QueueHandle_t test_resp_queue = NULL;

void setUp(void)
{
    mock_mqtt_reset();
    
    test_cmd_queue = xQueueCreate(10, sizeof(cmd_frame_t));
    test_resp_queue = xQueueCreate(10, sizeof(resp_frame_t));
    
    TEST_ASSERT_NOT_NULL(test_cmd_queue);
    TEST_ASSERT_NOT_NULL(test_resp_queue);
    
    ESP_LOGI(TAG, "🧪 MQTT events test setup completed");
}

void tearDown(void)
{
    transport_mqtt_cleanup();
    
    if (test_cmd_queue) {
        vQueueDelete(test_cmd_queue);
        test_cmd_queue = NULL;
    }
    
    if (test_resp_queue) {
        vQueueDelete(test_resp_queue);
        test_resp_queue = NULL;
    }
    
    ESP_LOGI(TAG, "🧹 MQTT events test teardown completed");
}

void test_mqtt_connected_event_updates_state(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT connected event updates state");
    
    // Initialize and start transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Initially should be disconnected
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should start disconnected");
    
    // Simulate connected event
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow event processing
    
    // Should now be connected
    TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "Should be connected after event");
}

void test_mqtt_disconnected_event_updates_state(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT disconnected event updates state");
    
    // Initialize, start, and connect
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "Should be connected");
    
    // Simulate disconnected event
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Should now be disconnected
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected after event");
}

void test_mqtt_connect_disconnect_cycle(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT connect/disconnect cycle");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Multiple connect/disconnect cycles
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "🔄 Cycle %d", i + 1);
        
        // Connect
        mock_mqtt_simulate_connected();
        vTaskDelay(pdMS_TO_TICKS(100));
        TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "Should be connected");
        
        // Disconnect
        mock_mqtt_simulate_disconnected();
        vTaskDelay(pdMS_TO_TICKS(100));
        TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected");
    }
}

void test_mqtt_events_without_init(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT events without initialization");
    
    // Try to simulate events without initialization
    // Should not crash
    mock_mqtt_simulate_connected();
    mock_mqtt_simulate_disconnected();
    mock_mqtt_simulate_data("test/topic", "test data", 9);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    TEST_PASS_MESSAGE("Events handled gracefully without init");
}

void test_mqtt_data_event_on_correct_topic(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT data event on correct topic");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Clear any existing messages in queue
    cmd_frame_t dummy;
    while (xQueueReceive(test_cmd_queue, &dummy, 0) == pdTRUE) {
        if (dummy.payload) free(dummy.payload);
    }
    
    // Simulate data on command topic
    const char *json_data = "{\"id\":1,\"op\":\"test\"}";
    mock_mqtt_simulate_data("smartdrip/cmd", json_data, strlen(json_data));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Should have received command
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Should receive command from correct topic");
    
    if (received_cmd.payload) {
        free(received_cmd.payload);
    }
}

void test_mqtt_data_event_on_wrong_topic(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT data event on wrong topic");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Clear queue
    cmd_frame_t dummy;
    while (xQueueReceive(test_cmd_queue, &dummy, 0) == pdTRUE) {
        if (dummy.payload) free(dummy.payload);
    }
    
    // Simulate data on wrong topic
    const char *json_data = "{\"id\":1,\"op\":\"test\"}";
    mock_mqtt_simulate_data("wrong/topic", json_data, strlen(json_data));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Should NOT have received command
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdFALSE, result, "Should not receive command from wrong topic");
}

void test_mqtt_events_while_disconnected(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT events while disconnected");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Stay disconnected (don't simulate connected event)
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected");
    
    // Try to simulate data event while disconnected
    const char *json_data = "{\"id\":1,\"op\":\"test\"}";
    mock_mqtt_simulate_data("smartdrip/cmd", json_data, strlen(json_data));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Command should still be processed (data events work regardless of connection state)
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Data events should work even when disconnected");
    
    if (received_cmd.payload) {
        free(received_cmd.payload);
    }
}

void test_mqtt_rapid_events(void)
{
    ESP_LOGI(TAG, "🧪 Testing rapid MQTT events");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Rapid connect/disconnect events
    for (int i = 0; i < 10; i++) {
        mock_mqtt_simulate_connected();
        mock_mqtt_simulate_disconnected();
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Final state should be disconnected
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected after rapid events");
    
    // Should still be functional
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "Should still work after rapid events");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting transport_mqtt event handling tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_mqtt_connected_event_updates_state);
    RUN_TEST(test_mqtt_disconnected_event_updates_state);
    RUN_TEST(test_mqtt_connect_disconnect_cycle);
    RUN_TEST(test_mqtt_events_without_init);
    RUN_TEST(test_mqtt_data_event_on_correct_topic);
    RUN_TEST(test_mqtt_data_event_on_wrong_topic);
    RUN_TEST(test_mqtt_events_while_disconnected);
    RUN_TEST(test_mqtt_rapid_events);
    
    UNITY_END();
}