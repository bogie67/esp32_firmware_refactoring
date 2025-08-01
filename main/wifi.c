
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "SVC_WIFI";

int8_t svc_wifi_scan(uint8_t **json_out, size_t *len)
{
    ESP_LOGI(TAG, "🔍 Starting WiFi scan...");
    
    wifi_scan_config_t cfg = { .show_hidden = true };
    esp_err_t ret = esp_wifi_scan_start(&cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start WiFi scan: %s", esp_err_to_name(ret));
        return -1;
    }
    
    ESP_LOGI(TAG, "📡 WiFi scan completed, getting results...");

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "📊 Found %d access points", ap_count);
    
    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!records) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for AP records");
        return -2;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, records);
    ESP_LOGI(TAG, "📋 Retrieved %d AP records", ap_count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "aps");
    for (int i = 0; i < ap_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddItemToArray(arr, item);
        ESP_LOGI(TAG, "📶 AP %d: %s (RSSI: %d)", i, records[i].ssid, records[i].rssi);
    }
    free(records);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    *json_out = (uint8_t*)str;
    *len = strlen(str);
    
    ESP_LOGI(TAG, "✅ WiFi scan completed successfully, JSON size: %zu bytes", *len);
    return 0;
}

int8_t svc_wifi_configure(const uint8_t *json_in, size_t len)
{
    if (!json_in || len == 0) return -1;
    cJSON *root = cJSON_ParseWithLength((const char*)json_in, len);
    if (!root) return -2;

    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(root, "pass");
    if (!cJSON_IsString(ssid)) { cJSON_Delete(root); return -3; }

    wifi_config_t cfg = {0};
    strncpy((char*)cfg.sta.ssid, ssid->valuestring, sizeof(cfg.sta.ssid));
    if (cJSON_IsString(pass))
        strncpy((char*)cfg.sta.password, pass->valuestring, sizeof(cfg.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_connect();

    cJSON_Delete(root);
    return (err == ESP_OK) ? 0 : -4;
}
