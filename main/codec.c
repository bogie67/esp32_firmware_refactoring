#include "codec.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

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
