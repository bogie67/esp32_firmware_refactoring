
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "transport_ble.h"
#include "transport_mqtt.h"
#include "cmd_proc.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "solenoid.h"


static const char *TAG = "APP_MAIN";
QueueHandle_t cmdQueue;
QueueHandle_t respQueue;

// Task rimosso - ora usiamo cmd_proc_start() da cmd_proc_task.c

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



#ifdef CONFIG_RUN_UNITY_TESTS
#include "unity.h"
#include "unity_test_runner.h"
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Starting NimBLE demo");

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
    cmd_proc_start();
    solenoid_init();

#if CONFIG_MAIN_WITH_BLE
   smart_ble_transport_init(cmdQueue, respQueue);
#endif

    // Inizializza e avvia transport MQTT
    transport_mqtt_init(cmdQueue, respQueue);
    transport_mqtt_start();
}
