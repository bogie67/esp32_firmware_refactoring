#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "test_wifi_simple.h"

static const char *TAG = "APP_MAIN_TEST";
QueueHandle_t cmdQueue;
QueueHandle_t respQueue;

static void wifi_stack_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    ESP_LOGI(TAG, "🧪 Starting WiFi Tests");

    // Inizializza NVS prima di WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    cmdQueue = xQueueCreate(10, sizeof(cmd_frame_t));
    respQueue = xQueueCreate(10, sizeof(resp_frame_t));

    wifi_stack_init();

    // Esegui i test
    run_wifi_tests();
    
    ESP_LOGI(TAG, "🎉 Test execution completed - system will continue normally");
    
    // Mantieni il sistema vivo dopo i test
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}