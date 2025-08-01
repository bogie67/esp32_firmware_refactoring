#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "unity.h"

static const char *TAG = "TEST_MAIN";

// Forward declaration dei test
void svc_wifi_scan_returns_json_with_mock_data(void);
void wifi_encode_decode_basic_functionality(void);

static void wifi_stack_init(void)
{
    ESP_LOGI(TAG, "🔧 Initializing WiFi stack for tests");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "✅ WiFi stack initialized");
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Starting ESP32 WiFi Unity Tests");

    // Inizializza NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inizializza WiFi stack
    wifi_stack_init();

    // Attendi un momento per la stabilizzazione
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "🧪 Starting Unity Test Framework");
    
    // Inizializza Unity
    UNITY_BEGIN();
    
    // Esegui i test
    ESP_LOGI(TAG, "🔍 Running WiFi scan test");
    RUN_TEST(svc_wifi_scan_returns_json_with_mock_data);
    
    ESP_LOGI(TAG, "🔧 Running WiFi configuration test");
    RUN_TEST(wifi_encode_decode_basic_functionality);
    
    // Finalizza Unity
    UNITY_END();
    
    ESP_LOGI(TAG, "🏁 All tests completed successfully");
}