
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
QueueHandle_t respQueue_BLE;
QueueHandle_t respQueue_MQTT;

// Task rimosso - ora usiamo cmd_proc_start() da cmd_proc_task.c

// Configurazione WiFi
// TODO: Implementare WiFi provisioning per configurazione dinamica delle credenziali
#define WIFI_SSID "BogieMobile"
#define WIFI_PASS "p@ssworD"
#define WIFI_MAXIMUM_RETRY 5

static int wifi_retry_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            wifi_retry_num++;
            ESP_LOGI(TAG, "🔄 Retry connessione WiFi (%d/%d)", wifi_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            ESP_LOGE(TAG, "❌ Connessione WiFi fallita dopo %d tentativi", WIFI_MAXIMUM_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ WiFi connesso! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_num = 0;
        
        // Avvia MQTT transport solo dopo aver ottenuto l'IP
        ESP_LOGI(TAG, "🚀 Avvio transport MQTT dopo connessione WiFi");
        transport_mqtt_start();
    }
}

static void wifi_stack_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "🌐 WiFi configurato per SSID: %s", WIFI_SSID);
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
    respQueue_BLE = xQueueCreate(10, sizeof(resp_frame_t));
    respQueue_MQTT = xQueueCreate(10, sizeof(resp_frame_t));

    wifi_stack_init();
    cmd_proc_start();
    solenoid_init();

#if CONFIG_MAIN_WITH_BLE
   smart_ble_transport_init(cmdQueue, respQueue_BLE);
#endif

    // Inizializza transport MQTT (start viene chiamato dopo connessione WiFi)
    transport_mqtt_init(cmdQueue, respQueue_MQTT);
}
