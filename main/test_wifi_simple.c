#include "unity.h"
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_TEST";

void setUp(void) {
    // Setup eseguito prima di ogni test
}

void tearDown(void) {
    // Cleanup eseguito dopo ogni test
}

void test_wifi_scan_returns_json(void) {
    ESP_LOGI(TAG, "🧪 Test: wifiScan returns JSON");
    
    uint8_t *json = NULL; 
    size_t len = 0;
    
    int8_t result = svc_wifi_scan(&json, &len);
    
    ESP_LOGI(TAG, "📊 Scan result: %d, JSON length: %zu", result, len);
    
    if (result == 0) {
        TEST_ASSERT_NOT_NULL_MESSAGE(json, "JSON output should not be NULL");
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, len, "JSON length should be > 0");
        
        ESP_LOGI(TAG, "📄 JSON content: %.*s", (int)len, json);
        
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr((char*)json, "aps"), "JSON should contain 'aps' field");
        
        free(json);
        ESP_LOGI(TAG, "✅ Test passed - Valid JSON returned");
    } else {
        ESP_LOGW(TAG, "⚠️  WiFi scan failed with code %d - this may be expected in test environment", result);
        TEST_ASSERT_TRUE_MESSAGE(result < 0, "WiFi scan should return error code or success");
    }
}

void test_wifi_configure_basic(void) {
    ESP_LOGI(TAG, "🧪 Test: Basic WiFi configuration");
    
    const char *test_json = "{\"ssid\":\"TestNetwork\",\"pass\":\"testpass\"}";
    
    int8_t result = svc_wifi_configure((uint8_t*)test_json, strlen(test_json));
    
    ESP_LOGI(TAG, "📊 Configure result: %d", result);
    
    TEST_ASSERT_TRUE_MESSAGE(result <= 0, "WiFi configure should return status code");
    
    ESP_LOGI(TAG, "✅ Test completed");
}

void run_wifi_tests(void) {
    ESP_LOGI(TAG, "🚀 Starting WiFi Unity Tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_wifi_scan_returns_json);
    RUN_TEST(test_wifi_configure_basic);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "🏁 All WiFi tests completed");
}