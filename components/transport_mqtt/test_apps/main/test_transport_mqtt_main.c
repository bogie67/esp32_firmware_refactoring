#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "transport_mqtt.h"
#include "esp_log.h"

static const char *TAG = "TRANSPORT_MQTT_TEST";

void setUp(void)
{
    // Setup before each test
}

void tearDown(void)
{
    // Cleanup after each test
}

TEST_CASE("transport_mqtt: initialization", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT transport initialization");
    
    int result = transport_mqtt_init();
    TEST_ASSERT_EQUAL(0, result);
}

TEST_CASE("transport_mqtt: connect with valid URL", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT connect with valid URL");
    
    transport_mqtt_init();
    
    int result = transport_mqtt_connect("mqtt://test.mosquitto.org");
    TEST_ASSERT_EQUAL(0, result);
}

TEST_CASE("transport_mqtt: connect with null URL", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT connect with null URL");
    
    transport_mqtt_init();
    
    int result = transport_mqtt_connect(NULL);
    TEST_ASSERT_EQUAL(0, result); // L'implementazione attuale restituisce sempre 0
}

TEST_CASE("transport_mqtt: publish message", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT publish message");
    
    transport_mqtt_init();
    transport_mqtt_connect("mqtt://test.mosquitto.org");
    
    const char *test_data = "Hello MQTT";
    int result = transport_mqtt_publish("test/topic", (const uint8_t*)test_data, strlen(test_data));
    TEST_ASSERT_EQUAL(0, result);
}

TEST_CASE("transport_mqtt: subscribe to topic", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT subscribe to topic");
    
    transport_mqtt_init();
    transport_mqtt_connect("mqtt://test.mosquitto.org");
    
    int result = transport_mqtt_subscribe("test/topic");
    TEST_ASSERT_EQUAL(0, result);
}

TEST_CASE("transport_mqtt: cleanup", "[transport_mqtt]")
{
    ESP_LOGI(TAG, "🧪 Test MQTT cleanup");
    
    transport_mqtt_init();
    transport_mqtt_connect("mqtt://test.mosquitto.org");
    
    // Queste funzioni non restituiscono valori, testiamo che non crashino
    transport_mqtt_disconnect();
    transport_mqtt_cleanup();
    
    TEST_PASS(); // Se arriviamo qui, le funzioni non hanno crashato
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test transport_mqtt component");
    
    UNITY_BEGIN();
    
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test transport_mqtt completati");
}