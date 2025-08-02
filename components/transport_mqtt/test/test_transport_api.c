#include "unity.h"
#include "transport_mqtt.h"
#include "mock_mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "TEST_TRANSPORT_API";

static QueueHandle_t test_cmd_queue = NULL;
static QueueHandle_t test_resp_queue = NULL;

void setUp(void)
{
    // Reset mock state before each test
    mock_mqtt_reset();
    
    // Create test queues
    test_cmd_queue = xQueueCreate(10, sizeof(cmd_frame_t));
    test_resp_queue = xQueueCreate(10, sizeof(resp_frame_t));
    
    TEST_ASSERT_NOT_NULL(test_cmd_queue);
    TEST_ASSERT_NOT_NULL(test_resp_queue);
    
    ESP_LOGI(TAG, "🧪 Test setup completed");
}

void tearDown(void)
{
    // Cleanup after each test
    transport_mqtt_cleanup();
    
    if (test_cmd_queue) {
        vQueueDelete(test_cmd_queue);
        test_cmd_queue = NULL;
    }
    
    if (test_resp_queue) {
        vQueueDelete(test_resp_queue);
        test_resp_queue = NULL;
    }
    
    ESP_LOGI(TAG, "🧹 Test teardown completed");
}

void test_transport_mqtt_init_with_valid_queues(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_init with valid queues");
    
    // Test initialization with valid queues
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    
    // Verify no errors (function should return without crashing)
    TEST_PASS_MESSAGE("transport_mqtt_init completed successfully");
}

void test_transport_mqtt_init_with_null_queues(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_init with NULL queues");
    
    // Test initialization with NULL queues (should handle gracefully)
    transport_mqtt_init(NULL, NULL);
    transport_mqtt_init(test_cmd_queue, NULL);
    transport_mqtt_init(NULL, test_resp_queue);
    
    // Should not crash
    TEST_PASS_MESSAGE("transport_mqtt_init handled NULL queues gracefully");
}

void test_transport_mqtt_start_calls_mqtt_client(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_start calls MQTT client");
    
    // Initialize transport first
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    
    // Start transport
    transport_mqtt_start();
    
    // Verify mock MQTT client was started
    TEST_ASSERT_TRUE_MESSAGE(mock_mqtt_is_started(), "MQTT client should be started");
}

void test_transport_mqtt_start_without_init(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_start without init");
    
    // Try to start without initialization
    transport_mqtt_start();
    
    // Should not crash and should not start MQTT client
    TEST_ASSERT_FALSE_MESSAGE(mock_mqtt_is_started(), "MQTT client should not be started without init");
}

void test_transport_mqtt_connection_state(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt connection state");
    
    // Initialize and start transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Initially should be disconnected
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected initially");
    
    // Simulate connection
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for event processing
    
    // Should now be connected
    TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "Should be connected after event");
    
    // Simulate disconnection
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for event processing
    
    // Should be disconnected again
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected after event");
}

void test_transport_mqtt_stop(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_stop");
    
    // Initialize and start transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Verify it's running and connected
    TEST_ASSERT_TRUE(mock_mqtt_is_started());
    TEST_ASSERT_TRUE(transport_mqtt_is_connected());
    
    // Stop transport
    transport_mqtt_stop();
    
    // Should be disconnected and stopped
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected after stop");
}

void test_transport_mqtt_cleanup(void)
{
    ESP_LOGI(TAG, "🧪 Testing transport_mqtt_cleanup");
    
    // Initialize and start transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Cleanup should not crash
    transport_mqtt_cleanup();
    
    // Connection state should be false
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "Should be disconnected after cleanup");
    
    // Starting again without re-init should not work
    transport_mqtt_start();
    TEST_ASSERT_FALSE_MESSAGE(mock_mqtt_is_started(), "Should not restart without re-init");
}

void test_transport_mqtt_lifecycle(void)
{
    ESP_LOGI(TAG, "🧪 Testing complete transport_mqtt lifecycle");
    
    // Complete lifecycle test
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE(transport_mqtt_is_connected());
    
    transport_mqtt_stop();
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    transport_mqtt_cleanup();
    TEST_ASSERT_FALSE(transport_mqtt_is_connected());
    
    TEST_PASS_MESSAGE("Complete lifecycle test passed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting transport_mqtt API tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_transport_mqtt_init_with_valid_queues);
    RUN_TEST(test_transport_mqtt_init_with_null_queues);
    RUN_TEST(test_transport_mqtt_start_calls_mqtt_client);
    RUN_TEST(test_transport_mqtt_start_without_init);
    RUN_TEST(test_transport_mqtt_connection_state);
    RUN_TEST(test_transport_mqtt_stop);
    RUN_TEST(test_transport_mqtt_cleanup);
    RUN_TEST(test_transport_mqtt_lifecycle);
    
    UNITY_END();
}