#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_TEST";

void setUp(void)
{
    // Setup before each test
}

void tearDown(void)
{
    // Cleanup after each test
}

TEST_CASE("wifi: scan function exists and callable", "[wifi]")
{
    ESP_LOGI(TAG, "🧪 Test wifi scan function callable");
    
    uint8_t *json_out = NULL;
    size_t len = 0;
    
    // Nota: Questo test non inizializza il WiFi stack per evitare configurazioni complesse
    // Si limita a verificare che la funzione sia callable
    int8_t result = svc_wifi_scan(&json_out, &len);
    
    // Il risultato potrebbe essere errore (-1) dato che WiFi non è inizializzato,
    // ma la funzione deve essere callable senza crash
    TEST_ASSERT(result == -1 || result == 0);
    
    if (json_out) {
        free(json_out);
    }
}

TEST_CASE("wifi: scan handles null parameters", "[wifi]")
{
    ESP_LOGI(TAG, "🧪 Test wifi scan with null parameters");
    
    // Test con parametri NULL
    int8_t result1 = svc_wifi_scan(NULL, NULL);
    TEST_ASSERT_EQUAL(-1, result1);
    
    size_t len;
    int8_t result2 = svc_wifi_scan(NULL, &len);
    TEST_ASSERT_EQUAL(-1, result2);
    
    uint8_t *json_out;
    int8_t result3 = svc_wifi_scan(&json_out, NULL);
    TEST_ASSERT_EQUAL(-1, result3);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test wifi component");
    
    // Inizializza NVS per evitare warning
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test wifi completati");
}