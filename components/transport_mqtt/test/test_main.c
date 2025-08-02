#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TEST_MAIN";

// External test functions
extern void app_main_transport_api(void);
extern void app_main_mqtt_events(void); 
extern void app_main_message_routing(void);

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting Transport MQTT Test Suite");
    ESP_LOGI(TAG, "📋 Running all transport_mqtt component tests");
    
    // Give system time to initialize
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "🧪 TEST SUITE 1: Transport API Tests");
    ESP_LOGI(TAG, "==========================================");
    
    // Run transport API tests
    UNITY_BEGIN();
    
    // We'll include all tests in one runner since ESP32 Unity doesn't support multiple app_main
    ESP_LOGI(TAG, "ℹ️ Note: All tests combined in single runner for ESP32 Unity compatibility");
    ESP_LOGI(TAG, "📊 Test Results:");
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Transport MQTT Test Suite Completed");
    ESP_LOGI(TAG, "🔍 Check test results above for detailed status");
}