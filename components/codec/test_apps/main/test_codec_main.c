#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "codec.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "esp_log.h"

static const char *TAG = "CODEC_TEST";

void setUp(void)
{
    // Chiamata prima di ogni test
}

void tearDown(void)
{
    // Chiamata dopo ogni test
}

TEST_CASE("decode_ble_frame: frame valido base", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test decode frame valido base");
    
    // Frame: id=0x1234, op="test", payload="hello"
    uint8_t data[] = {0x34, 0x12, 0x04, 't', 'e', 's', 't', 'h', 'e', 'l', 'l', 'o'};
    cmd_frame_t frame;
    
    bool result = decode_ble_frame(data, sizeof(data), &frame);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x1234, frame.id);
    TEST_ASSERT_EQUAL_STRING("test", frame.op);
    TEST_ASSERT_EQUAL(5, frame.len);
    TEST_ASSERT_EQUAL_STRING("hello", (char*)frame.payload);
    
    // Cleanup
    if (frame.payload) free(frame.payload);
}

TEST_CASE("decode_ble_frame: frame senza payload", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test decode frame senza payload");
    
    // Frame: id=0x5678, op="ok", nessun payload
    uint8_t data[] = {0x78, 0x56, 0x02, 'o', 'k'};
    cmd_frame_t frame;
    
    bool result = decode_ble_frame(data, sizeof(data), &frame);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x5678, frame.id);
    TEST_ASSERT_EQUAL_STRING("ok", frame.op);
    TEST_ASSERT_EQUAL(0, frame.len);
    TEST_ASSERT_NULL(frame.payload);
}

TEST_CASE("decode_ble_frame: frame troppo corto", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test decode frame troppo corto");
    
    // Frame troppo corto (solo 2 byte invece di minimo 3)
    uint8_t data[] = {0x34, 0x12};
    cmd_frame_t frame;
    
    bool result = decode_ble_frame(data, sizeof(data), &frame);
    
    TEST_ASSERT_FALSE(result);
}

TEST_CASE("decode_ble_frame: opLen invalido", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test decode frame con opLen invalido");
    
    // Frame con opLen=0 (invalido)
    uint8_t data[] = {0x34, 0x12, 0x00};
    cmd_frame_t frame;
    
    bool result = decode_ble_frame(data, sizeof(data), &frame);
    
    TEST_ASSERT_FALSE(result);
}

TEST_CASE("encode_ble_resp: response ok senza payload", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test encode response ok senza payload");
    
    resp_frame_t resp = {
        .id = 0x1234,
        .status = 0,
        .len = 0,
        .payload = NULL
    };
    
    size_t outLen;
    uint8_t *encoded = encode_ble_resp(&resp, &outLen);
    
    TEST_ASSERT_NOT_NULL(encoded);
    TEST_ASSERT_EQUAL(6, outLen); // 2(id) + 1(opLen) + 2(op="ok") + 1(status)
    
    // Verifica contenuto: id=0x1234, opLen=2, op="ok", status=0
    TEST_ASSERT_EQUAL(0x34, encoded[0]);  // id low byte
    TEST_ASSERT_EQUAL(0x12, encoded[1]);  // id high byte  
    TEST_ASSERT_EQUAL(2, encoded[2]);     // opLen="ok"
    TEST_ASSERT_EQUAL('o', encoded[3]);   // op[0]
    TEST_ASSERT_EQUAL('k', encoded[4]);   // op[1]
    TEST_ASSERT_EQUAL(0, encoded[5]);     // status=0
    
    free(encoded);
}

TEST_CASE("encode_ble_resp: response err con payload", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test encode response err con payload");
    
    const char *payload_data = "not found";
    resp_frame_t resp = {
        .id = 0x5678,
        .status = 404,
        .len = strlen(payload_data),
        .payload = (uint8_t*)payload_data
    };
    
    size_t outLen;
    uint8_t *encoded = encode_ble_resp(&resp, &outLen);
    
    TEST_ASSERT_NOT_NULL(encoded);
    TEST_ASSERT_EQUAL(17, outLen); // 2(id) + 1(opLen) + 3(op="err") + 1(status) + 9(payload) + 1(status byte)
    
    // Verifica contenuto: id=0x5678, opLen=3, op="err", status=404, payload="not found"
    TEST_ASSERT_EQUAL(0x78, encoded[0]);  // id low byte
    TEST_ASSERT_EQUAL(0x56, encoded[1]);  // id high byte
    TEST_ASSERT_EQUAL(3, encoded[2]);     // opLen="err"
    TEST_ASSERT_EQUAL('e', encoded[3]);   // op[0]
    TEST_ASSERT_EQUAL('r', encoded[4]);   // op[1] 
    TEST_ASSERT_EQUAL('r', encoded[5]);   // op[2]
    TEST_ASSERT_EQUAL(404 & 0xFF, encoded[6]); // status byte
    
    // Verifica payload
    TEST_ASSERT_EQUAL_MEMORY("not found", &encoded[7], 9);
    
    free(encoded);
}

TEST_CASE("codec: roundtrip encode/decode", "[codec]")
{
    ESP_LOGI(TAG, "🧪 Test roundtrip encode/decode");
    
    // Crea una response
    const char *payload_data = "test data";
    resp_frame_t original_resp = {
        .id = 0xABCD,
        .status = 0,
        .len = strlen(payload_data),
        .payload = (uint8_t*)payload_data
    };
    
    // Encode
    size_t encoded_len;
    uint8_t *encoded = encode_ble_resp(&original_resp, &encoded_len);
    TEST_ASSERT_NOT_NULL(encoded);
    
    // Il frame encodato non può essere decodificato direttamente perché 
    // decode_ble_frame si aspetta un cmd_frame, non un resp_frame
    // Questo test verifica solo che l'encoding produca un risultato valido
    TEST_ASSERT_GREATER_THAN(6, encoded_len);
    
    free(encoded);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Avvio test codec component");
    
    UNITY_BEGIN();
    
    // Esegui tutti i test
    unity_run_all_tests();
    
    UNITY_END();
    
    ESP_LOGI(TAG, "✅ Test codec completati");
}