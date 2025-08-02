#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "schedule.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "SCHEDULE_TEST";

void setUp(void)
{
    // Setup before each test
}

void tearDown(void)
{
    // Cleanup after each test
}

TEST_CASE("schedule: sync with null parameters", "[schedule]")
{
    ESP_LOGI(TAG, "🧪 Test schedule sync with null parameters");
    
    int8_t result1 = svc_sync_schedule(NULL, 0);
    TEST_ASSERT_EQUAL(-2, result1);
    
    int8_t result2 = svc_sync_schedule(NULL, 100);
    TEST_ASSERT_EQUAL(-2, result2);
}

TEST_CASE("schedule: sync with empty JSON", "[schedule]")
{
    ESP_LOGI(TAG, "🧪 Test schedule sync with empty data");
    
    const char *empty_data = "";
    int8_t result = svc_sync_schedule((const uint8_t*)empty_data, 0);
    TEST_ASSERT_EQUAL(-2, result);
}

TEST_CASE("schedule: sync with invalid JSON", "[schedule]")
{
    ESP_LOGI(TAG, "🧪 Test schedule sync with malformed JSON");
    
    const char *invalid_json = "{invalid json";
    int8_t result = svc_sync_schedule((const uint8_t*)invalid_json, strlen(invalid_json));
    TEST_ASSERT_EQUAL(-3, result);
}

TEST_CASE("schedule: sync with valid JSON", "[schedule]")
{
    ESP_LOGI(TAG, "🧪 Test schedule sync with valid JSON");
    
    // JSON valido con schedule di esempio
    const char *valid_json = "{\"schedules\":[{\"id\":1,\"time\":\"08:00\",\"duration\":300}]}";
    int8_t result = svc_sync_schedule((const uint8_t*)valid_json, strlen(valid_json));
    
    // Dovrebbe restituire 0 (successo) - l'implementazione attuale restituisce sempre 0
    TEST_ASSERT_EQUAL(0, result);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test schedule component");
    
    // Inizializza NVS per evitare problemi con schedule storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test schedule completati");
}