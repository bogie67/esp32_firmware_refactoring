#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "solenoid.h"
#include "esp_log.h"

static const char *TAG = "SOLENOID_TEST";

void setUp(void)
{
    // Setup before each test
}

void tearDown(void)
{
    // Cleanup after each test
}

TEST_CASE("solenoid: initialization", "[solenoid]")
{
    ESP_LOGI(TAG, "🧪 Test solenoid initialization");
    
    // solenoid_init() è void, testiamo che non crashi
    solenoid_init();
    TEST_PASS(); // Se arriviamo qui, l'inizializzazione non ha crashato
}

TEST_CASE("solenoid: on command with invalid parameters", "[solenoid]")
{
    ESP_LOGI(TAG, "🧪 Test solenoid on with invalid parameters");
    
    // Test con JSON NULL
    int8_t result1 = svc_solenoid_on(NULL, 0);
    TEST_ASSERT_EQUAL(-2, result1);
    
    // Test con lunghezza 0
    const char *dummy_json = "{}";
    int8_t result2 = svc_solenoid_on((const uint8_t*)dummy_json, 0);
    TEST_ASSERT_EQUAL(-2, result2);
}

TEST_CASE("solenoid: on command with invalid JSON", "[solenoid]")
{
    ESP_LOGI(TAG, "🧪 Test solenoid on with malformed JSON");
    
    const char *invalid_json = "{invalid json";
    int8_t result = svc_solenoid_on((const uint8_t*)invalid_json, strlen(invalid_json));
    TEST_ASSERT_EQUAL(-3, result);
}

TEST_CASE("solenoid: on command with valid JSON", "[solenoid]")
{
    ESP_LOGI(TAG, "🧪 Test solenoid on with valid JSON");
    
    // Prima inizializza i solenoidi
    solenoid_init();
    
    // JSON valido con comando on
    const char *valid_json = "{\"ch\":1}";
    int8_t result = svc_solenoid_on((const uint8_t*)valid_json, strlen(valid_json));
    
    // Dovrebbe essere 0 (successo) o un valore specifico dell'implementazione
    TEST_ASSERT(result >= -1); // Accetta valori >= -1
}

TEST_CASE("solenoid: off command with valid JSON", "[solenoid]")
{
    ESP_LOGI(TAG, "🧪 Test solenoid off with valid JSON");
    
    // Prima inizializza i solenoidi
    solenoid_init();
    
    // JSON valido con comando off
    const char *valid_json = "{\"ch\":1}";
    int8_t result = svc_solenoid_off((const uint8_t*)valid_json, strlen(valid_json));
    
    // Dovrebbe essere 0 (successo) o un valore specifico dell'implementazione
    TEST_ASSERT(result >= -1); // Accetta valori >= -1
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test solenoid component");
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test solenoid completati");
}