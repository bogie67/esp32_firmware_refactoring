
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "error_manager.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "transport_ble.h"
#include "transport_mqtt.h"
#include "cmd_proc.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "security1_session.h"

#include "solenoid.h"


static const char *TAG = "APP_MAIN";

// WiFi Event Group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
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
        
        // Segnala al main task di avviare MQTT Security1
        ESP_LOGI(TAG, "🚀 WiFi connesso - signaling MQTT Security1 start");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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

    // Inizializza framework error management unificato
    esp_err_t err = error_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize error manager: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "🎯 Unified error management system initialized");

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

    // Crea Event Group per sincronizzazione WiFi
    s_wifi_event_group = xEventGroupCreate();

    wifi_stack_init();
    cmd_proc_start();
    solenoid_init();
    
    // Inizializza framework Security1
    ESP_LOGI(TAG, "🔐 Initializing Security1 framework");
    esp_err_t sec1_init_ret = security1_session_init();
    if (sec1_init_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize Security1 framework: %s", esp_err_to_name(sec1_init_ret));
        return;
    }
    ESP_LOGI(TAG, "✅ Security1 framework initialized");

#if CONFIG_MAIN_WITH_BLE
   smart_ble_transport_init(cmdQueue, respQueue_BLE);
#endif

    // Inizializza transport MQTT (start viene chiamato dopo connessione WiFi)
    transport_mqtt_init(cmdQueue, respQueue_MQTT);
    
    // Aspetta connessione WiFi e avvia MQTT Security1
    ESP_LOGI(TAG, "⏳ Waiting for WiFi connection...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    
    ESP_LOGI(TAG, "🚀 Starting MQTT Security1 transport");
    
    // Configurazione Security1 MQTT nel main task (stack più grande)
    transport_mqtt_security1_config_t sec1_config = {
        .broker_uri = CONFIG_MQTT_BROKER_URI,
        .topic_prefix = "security1/esp32",
        .client_id = "SmartDrip_ESP32_Sec1",
        .proof_of_possession = "test_pop_12345",
        .qos_level = 1,
        .keepalive_interval = 60
    };
    
    esp_err_t mqtt_ret = transport_mqtt_start_with_security1(cmdQueue, respQueue_MQTT, &sec1_config);
    if (mqtt_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start MQTT Security1: %s", esp_err_to_name(mqtt_ret));
    } else {
        ESP_LOGI(TAG, "✅ MQTT Security1 started successfully");
    }
}
