#include "schedule.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "SVC_SCHEDULE";

/* Parse JSON e salva su NVS (placeholder) */
int8_t svc_sync_schedule(const uint8_t *json, size_t len)
{
    if (!json || len == 0) return -2;

    cJSON *root = cJSON_ParseWithLength((const char *)json, len);
    if (!root) {
        ESP_LOGE(TAG, "JSON malformato");
        return -3;
    }

    const cJSON *zones = cJSON_GetObjectItem(root, "zones");
    if (!cJSON_IsArray(zones)) {
        cJSON_Delete(root);
        return -4;
    }

    ESP_LOGI(TAG, "Schedule con %d zone", cJSON_GetArraySize(zones));
    /* TODO: persistere in NVS */

    cJSON_Delete(root);
    return 0;
}
