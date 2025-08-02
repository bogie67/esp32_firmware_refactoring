#include "codec.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"

bool decode_ble_frame(const uint8_t *data, size_t len, cmd_frame_t *out)
{
    memset(out, 0, sizeof(cmd_frame_t));
    ESP_LOGI("CODEC", "🔍 Decode frame: len=%zu, first bytes: %02x %02x %02x", 
             len, len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0);
    if (len < 3) {
        ESP_LOGW("CODEC", "❌ Frame troppo corto: len=%zu < 3", len);
        return false;
    }

    out->id      = data[0] | (data[1] << 8);
    uint8_t opLen = data[2];
    ESP_LOGI("CODEC", "📋 Parsed: id=%u, opLen=%u, expected_total=%u", out->id, opLen, 3 + opLen);
    if (opLen == 0 || opLen > 15 || (3 + opLen) > len) {
        ESP_LOGW("CODEC", "❌ Invalid opLen=%u or frame too short (need %u, have %zu)", opLen, 3 + opLen, len);
        return false;
    }

    memcpy(out->op, &data[3], opLen);
    out->op[opLen] = '\0';

    out->len = len - (3 + opLen);
    if (out->len) {
        out->payload = malloc(out->len);
        if (!out->payload) return false;
        memcpy(out->payload, &data[3 + opLen], out->len);
    }
    return true;
}

uint8_t *encode_ble_resp(const resp_frame_t *r, size_t *outLen)
{
    const char *opStr = (r->status == 0) ? "ok" : "err";
    uint8_t opLen = strlen(opStr);

    size_t total = 2 + 1 + opLen + 1 + r->len;
    uint8_t *buf = malloc(total);
    if (!buf) return NULL;

    buf[0] = r->id & 0xFF;
    buf[1] = r->id >> 8;
    buf[2] = opLen;
    memcpy(&buf[3], opStr, opLen);
    buf[3 + opLen] = (uint8_t)r->status;

    if (r->len && r->payload)
        memcpy(&buf[4 + opLen], r->payload, r->len);

    *outLen = total;
    return buf;
}

// JSON support for MQTT transport
bool decode_json_command(const char *json_data, size_t len, cmd_frame_t *out)
{
    memset(out, 0, sizeof(cmd_frame_t));
    
    // Ensure null termination for cJSON
    char *json_str = malloc(len + 1);
    if (!json_str) {
        ESP_LOGE("CODEC", "❌ Memoria insufficiente per JSON string");
        return false;
    }
    memcpy(json_str, json_data, len);
    json_str[len] = '\0';
    
    ESP_LOGI("CODEC", "🔍 Decode JSON: %s", json_str);
    
    cJSON *json = cJSON_Parse(json_str);
    free(json_str);
    
    if (!json) {
        ESP_LOGE("CODEC", "❌ JSON parse error");
        return false;
    }
    
    // Parse required fields
    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *op = cJSON_GetObjectItem(json, "op");
    
    // Debug detailed parsing
    ESP_LOGI("CODEC", "🔍 Parsing fields: id=%p, op=%p", id, op);
    if (id) {
        ESP_LOGI("CODEC", "🔍 id type: %s, value: %d", cJSON_IsNumber(id) ? "number" : "not_number", id->valueint);
    } else {
        ESP_LOGE("CODEC", "❌ id field not found in JSON");
    }
    if (op) {
        ESP_LOGI("CODEC", "🔍 op type: %s, value: %s", cJSON_IsString(op) ? "string" : "not_string", op->valuestring);
    } else {
        ESP_LOGE("CODEC", "❌ op field not found in JSON");
    }
    
    if (!cJSON_IsNumber(id) || !cJSON_IsString(op)) {
        ESP_LOGE("CODEC", "❌ Missing required fields id or op");
        cJSON_Delete(json);
        return false;
    }
    
    out->id = (uint16_t)id->valueint;
    strncpy(out->op, op->valuestring, sizeof(out->op) - 1);
    out->op[sizeof(out->op) - 1] = '\0';
    
    // Parse optional payload
    cJSON *payload = cJSON_GetObjectItem(json, "payload");
    if (payload && cJSON_IsString(payload)) {
        size_t payload_len = strlen(payload->valuestring);
        if (payload_len > 0) {
            out->payload = malloc(payload_len + 1);
            if (out->payload) {
                strcpy((char*)out->payload, payload->valuestring);
                out->len = payload_len;
            }
        }
    }
    
    cJSON_Delete(json);
    ESP_LOGI("CODEC", "✅ JSON parsed: id=%u, op=%s, payload_len=%u", 
             out->id, out->op, out->len);
    return true;
}

char *encode_json_response(const resp_frame_t *r)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE("CODEC", "❌ Failed to create JSON object");
        return NULL;
    }
    
    cJSON_AddNumberToObject(json, "id", r->id);
    cJSON_AddNumberToObject(json, "status", r->status);
    cJSON_AddBoolToObject(json, "is_final", r->is_final);
    
    if (r->len > 0 && r->payload) {
        // Assume payload is string for JSON
        char *payload_str = malloc(r->len + 1);
        if (payload_str) {
            memcpy(payload_str, r->payload, r->len);
            payload_str[r->len] = '\0';
            cJSON_AddStringToObject(json, "payload", payload_str);
            free(payload_str);
        }
    } else {
        cJSON_AddNullToObject(json, "payload");
    }
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (json_string) {
        ESP_LOGI("CODEC", "✅ JSON response encoded: %s", json_string);
    } else {
        ESP_LOGE("CODEC", "❌ Failed to encode JSON response");
    }
    
    return json_string;
}
