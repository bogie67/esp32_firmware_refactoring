#include "unity.h"
#include "transport_mqtt.h"
#include "mock_mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "TEST_BACKOFF";

static QueueHandle_t test_cmd_queue = NULL;
static QueueHandle_t test_resp_queue = NULL;

// External variables from transport_mqtt.c (friend access for testing)
extern uint32_t backoff_delay_ms;
extern esp_timer_handle_t reconnect_timer;
extern mqtt_state_t mqtt_state;

void setUp(void)
{
    mock_mqtt_reset();
    
    test_cmd_queue = xQueueCreate(10, sizeof(cmd_frame_t));
    test_resp_queue = xQueueCreate(10, sizeof(resp_frame_t));
    
    TEST_ASSERT_NOT_NULL(test_cmd_queue);
    TEST_ASSERT_NOT_NULL(test_resp_queue);
    
    ESP_LOGI(TAG, "🧪 Back-off test setup completed");
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
    
    ESP_LOGI(TAG, "🧹 Back-off test teardown completed");
}

void test_backoff_doubles_on_error(void)
{
    ESP_LOGI(TAG, "🧪 Testing back-off doubles on error");
    
    // Initialize transport
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Verify initial state
    TEST_ASSERT_EQUAL_MESSAGE(MQTT_DOWN, transport_mqtt_get_state(), "Should start in DOWN state");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_INITIAL_MS, backoff_delay_ms, "Should start with initial back-off");
    
    // Simulate first error
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow event processing
    
    // Back-off should double after first error
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_INITIAL_MS * 2, backoff_delay_ms, "Back-off should double after first error");
    TEST_ASSERT_TRUE_MESSAGE(esp_timer_is_active(reconnect_timer), "Reconnect timer should be active");
    
    // Simulate second error (timer callback will trigger)
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Back-off should double again
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_INITIAL_MS * 4, backoff_delay_ms, "Back-off should double again");
}

void test_backoff_respects_maximum(void)
{
    ESP_LOGI(TAG, "🧪 Testing back-off respects maximum");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Simulate multiple errors to reach maximum
    uint32_t expected_delay = CONFIG_MQTT_BACKOFF_INITIAL_MS;
    
    for (int i = 0; i < 10; i++) {
        mock_mqtt_simulate_disconnected();
        vTaskDelay(pdMS_TO_TICKS(50));
        
        expected_delay = (expected_delay * 2 > CONFIG_MQTT_BACKOFF_MAX_MS) 
                        ? CONFIG_MQTT_BACKOFF_MAX_MS 
                        : expected_delay * 2;
    }
    
    // Should not exceed maximum
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_MAX_MS, backoff_delay_ms, 
                                      "Back-off should not exceed maximum");
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_MAX_MS, backoff_delay_ms, 
                             "Back-off should reach maximum");
}

void test_backoff_resets_on_connection(void)
{
    ESP_LOGI(TAG, "🧪 Testing back-off resets on successful connection");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Simulate several errors to increase back-off
    for (int i = 0; i < 3; i++) {
        mock_mqtt_simulate_disconnected();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Verify back-off increased
    uint32_t increased_backoff = backoff_delay_ms;
    TEST_ASSERT_GREATER_THAN_MESSAGE(CONFIG_MQTT_BACKOFF_INITIAL_MS, increased_backoff, 
                                     "Back-off should have increased");
    
    // Simulate successful connection
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Verify back-off reset and timer stopped
    TEST_ASSERT_EQUAL_MESSAGE(CONFIG_MQTT_BACKOFF_INITIAL_MS, backoff_delay_ms, 
                             "Back-off should reset to initial value");
    TEST_ASSERT_EQUAL_MESSAGE(MQTT_UP, transport_mqtt_get_state(), "State should be UP");
    TEST_ASSERT_FALSE_MESSAGE(esp_timer_is_active(reconnect_timer), 
                             "Reconnect timer should be stopped");
}

void test_state_machine_transitions(void)
{
    ESP_LOGI(TAG, "🧪 Testing state machine transitions");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // Should start in DOWN state
    TEST_ASSERT_EQUAL_MESSAGE(MQTT_DOWN, transport_mqtt_get_state(), "Should start DOWN");
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "is_connected should be false");
    
    // Simulate connection
    mock_mqtt_simulate_connected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(MQTT_UP, transport_mqtt_get_state(), "Should be UP after connect");
    TEST_ASSERT_TRUE_MESSAGE(transport_mqtt_is_connected(), "is_connected should be true");
    
    // Simulate disconnection
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    TEST_ASSERT_EQUAL_MESSAGE(MQTT_DOWN, transport_mqtt_get_state(), "Should be DOWN after disconnect");
    TEST_ASSERT_FALSE_MESSAGE(transport_mqtt_is_connected(), "is_connected should be false");
}

void test_timer_not_double_scheduled(void)
{
    ESP_LOGI(TAG, "🧪 Testing timer is not double-scheduled");
    
    transport_mqtt_init(test_cmd_queue, test_resp_queue);
    transport_mqtt_start();
    
    // First error should start timer
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    TEST_ASSERT_TRUE_MESSAGE(esp_timer_is_active(reconnect_timer), "Timer should be active after first error");
    
    uint32_t first_backoff = backoff_delay_ms;
    
    // Second error while timer active should not double-schedule
    mock_mqtt_simulate_disconnected();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Back-off should still be the same (not doubled again)
    TEST_ASSERT_EQUAL_MESSAGE(first_backoff, backoff_delay_ms, 
                             "Back-off should not double when timer already active");
    TEST_ASSERT_TRUE_MESSAGE(esp_timer_is_active(reconnect_timer), "Timer should still be active");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting transport_mqtt back-off tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_backoff_doubles_on_error);
    RUN_TEST(test_backoff_respects_maximum);
    RUN_TEST(test_backoff_resets_on_connection);
    RUN_TEST(test_state_machine_transitions);
    RUN_TEST(test_timer_not_double_scheduled);
    
    UNITY_END();
}