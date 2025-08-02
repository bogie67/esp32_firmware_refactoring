#include "unity.h"
#include "transport_mqtt.h"
#include "mock_mqtt_client.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "TEST_MESSAGE_ROUTING";

static QueueHandle_t test_cmd_queue = NULL;
static QueueHandle_t test_resp_queue = NULL;

void setUp(void)
{
    mock_mqtt_reset();
    
    test_cmd_queue = xQueueCreate(10, sizeof(cmd_frame_t));
    test_resp_queue = xQueueCreate(10, sizeof(resp_frame_t));
    
    TEST_ASSERT_NOT_NULL(test_cmd_queue);
    TEST_ASSERT_NOT_NULL(test_resp_queue);
    
    // Initialize and start transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow event processing
    
    ESP_LOGI(TAG, "🧪 Message routing test setup completed");
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
    
    ESP_LOGI(TAG, "🧹 Message routing test teardown completed");
}

void test_mqtt_command_reaches_queue(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT command reaches command queue");
    
    // Simulate incoming MQTT command
    const char *json_cmd = "{\"id\":1,\"op\":\"ping\"}";
    mock_mqtt_simulate_data("smartdrip/cmd", json_cmd, strlen(json_cmd));
    
    // Give time for processing
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Check if command was added to queue
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Command should be in queue");
    TEST_ASSERT_EQUAL_MESSAGE(1, received_cmd.id, "Command ID should be 1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ping", received_cmd.op, "Command op should be 'ping'");
    TEST_ASSERT_EQUAL_MESSAGE(ORIGIN_MQTT, received_cmd.origin, "Command origin should be MQTT");
}

void test_mqtt_command_with_payload(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT command with payload");
    
    // Simulate incoming MQTT command with payload
    const char *json_cmd = "{\"id\":2,\"op\":\"led\",\"payload\":\"on\"}";
    mock_mqtt_simulate_data("smartdrip/cmd", json_cmd, strlen(json_cmd));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Check command
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Command should be in queue");
    TEST_ASSERT_EQUAL_MESSAGE(2, received_cmd.id, "Command ID should be 2");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("led", received_cmd.op, "Command op should be 'led'");
    TEST_ASSERT_EQUAL_MESSAGE(ORIGIN_MQTT, received_cmd.origin, "Command origin should be MQTT");
    TEST_ASSERT_NOT_NULL_MESSAGE(received_cmd.payload, "Payload should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("on", (char*)received_cmd.payload, "Payload should be 'on'");
    
    // Cleanup payload
    if (received_cmd.payload) {
        free(received_cmd.payload);
    }
}

void test_invalid_json_command_ignored(void)
{
    ESP_LOGI(TAG, "🧪 Testing invalid JSON command is ignored");
    
    // Simulate invalid JSON
    const char *invalid_json = "{\"id\":1,\"invalid\":\"json\""; // Missing closing brace
    mock_mqtt_simulate_data("smartdrip/cmd", invalid_json, strlen(invalid_json));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Check that no command was added to queue
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(pdFALSE, result, "Invalid command should not be in queue");
}

void test_wrong_topic_ignored(void)
{
    ESP_LOGI(TAG, "🧪 Testing message on wrong topic is ignored");
    
    // Simulate message on different topic
    const char *json_cmd = "{\"id\":1,\"op\":\"ping\"}";
    mock_mqtt_simulate_data("wrong/topic", json_cmd, strlen(json_cmd));
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Check that no command was added to queue
    cmd_frame_t received_cmd;
    BaseType_t result = xQueueReceive(test_cmd_queue, &received_cmd, pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(pdFALSE, result, "Command on wrong topic should not be in queue");
}

void test_mqtt_response_published(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT response is published");
    
    // Create a response for MQTT origin
    resp_frame_t response = {
        .id = 1,
        .status = 0, // Success
        .is_final = true,
        .origin = ORIGIN_MQTT,
        .payload = NULL,
        .len = 0
    };
    
    // Send response to response queue
    BaseType_t result = xQueueSend(test_resp_queue, &response, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Response should be sent to queue");
    
    // Give time for TX task to process
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Verify response was published via mock
    const char *published_topic = mock_mqtt_get_last_published_topic();
    const char *published_data = mock_mqtt_get_last_published_data();
    
    TEST_ASSERT_NOT_NULL_MESSAGE(published_topic, "Published topic should not be NULL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("smartdrip/resp", published_topic, "Should publish to resp topic");
    TEST_ASSERT_NOT_NULL_MESSAGE(published_data, "Published data should not be NULL");
    
    // Check JSON response format
    TEST_ASSERT_TRUE_MESSAGE(strstr(published_data, "\"id\":1") != NULL, "Response should contain id:1");
    TEST_ASSERT_TRUE_MESSAGE(strstr(published_data, "\"status\":0") != NULL, "Response should contain status:0");
}

void test_ble_response_not_published(void)
{
    ESP_LOGI(TAG, "🧪 Testing BLE response is not published to MQTT");
    
    // Create a response for BLE origin (should be ignored by MQTT transport)
    resp_frame_t response = {
        .id = 1,
        .status = 0,
        .is_final = true,
        .origin = ORIGIN_BLE, // BLE origin
        .payload = NULL,
        .len = 0
    };
    
    // Reset mock state
    mock_mqtt_reset();
    
    // Send response to response queue
    BaseType_t result = xQueueSend(test_resp_queue, &response, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Response should be sent to queue");
    
    // Give time for TX task to process
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Verify no response was published
    const char *published_topic = mock_mqtt_get_last_published_topic();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", published_topic, "No topic should be published for BLE response");
}

void test_response_with_payload(void)
{
    ESP_LOGI(TAG, "🧪 Testing MQTT response with payload");
    
    // Create response with payload
    const char *payload_data = "sensor_value_42";
    char *response_payload = malloc(strlen(payload_data) + 1);
    strcpy(response_payload, payload_data);
    
    resp_frame_t response = {
        .id = 3,
        .status = 0,
        .is_final = true,
        .origin = ORIGIN_MQTT,
        .payload = (uint8_t*)response_payload,
        .len = strlen(payload_data)
    };
    
    // Send response
    BaseType_t result = xQueueSend(test_resp_queue, &response, pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, result, "Response should be sent to queue");
    
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Verify response was published with payload
    const char *published_data = mock_mqtt_get_last_published_data();
    TEST_ASSERT_NOT_NULL_MESSAGE(published_data, "Published data should not be NULL");
    TEST_ASSERT_TRUE_MESSAGE(strstr(published_data, "\"id\":3") != NULL, "Response should contain id:3");
    TEST_ASSERT_TRUE_MESSAGE(strstr(published_data, "sensor_value_42") != NULL, "Response should contain payload");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting transport_mqtt message routing tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_mqtt_command_reaches_queue);
    RUN_TEST(test_mqtt_command_with_payload);
    RUN_TEST(test_invalid_json_command_ignored);
    RUN_TEST(test_wrong_topic_ignored);
    RUN_TEST(test_mqtt_response_published);
    RUN_TEST(test_ble_response_not_published);
    RUN_TEST(test_response_with_payload);
    
    UNITY_END();
}