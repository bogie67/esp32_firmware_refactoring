#include "unity.h"
#include "transport_ble.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Test elementari senza BLE reale
TEST_CASE("transport_ble header includes work", "[transport_ble]")
{
    // Test che gli header si includano senza errori
    TEST_ASSERT_TRUE(true);
}

TEST_CASE("transport_ble can create queues", "[transport_ble]") 
{
    // Test che possiamo creare le queue richieste  
    QueueHandle_t cmd_q = xQueueCreate(4, sizeof(int));
    QueueHandle_t resp_q = xQueueCreate(4, sizeof(int));
    
    TEST_ASSERT_NOT_NULL(cmd_q);
    TEST_ASSERT_NOT_NULL(resp_q);
    
    vQueueDelete(cmd_q);
    vQueueDelete(resp_q);
}

TEST_CASE("transport_ble init function exists", "[transport_ble]")
{
    // Test che la funzione di init esista (senza chiamarla per evitare crash BLE)
    void (*init_func)(QueueHandle_t, QueueHandle_t) = smart_ble_transport_init;
    TEST_ASSERT_NOT_NULL(init_func);
}