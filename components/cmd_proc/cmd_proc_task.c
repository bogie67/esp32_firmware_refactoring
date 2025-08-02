
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "error_manager.h"
#include <string.h>
#include <stdlib.h>
#include "cmd_frame.h"
#include "resp_frame.h"
#include "schedule.h"
#include "wifi.h"

extern QueueHandle_t cmdQueue;
extern QueueHandle_t respQueue_BLE;
extern QueueHandle_t respQueue_MQTT;
static const char *TAG = "CMD_PROC";

static resp_frame_t handle(const cmd_frame_t *cmd)
{
    resp_frame_t r = {.id=cmd->id,.origin=cmd->origin,.payload=NULL,.len=0,.is_final=true};
    if (strcmp(cmd->op,"syncSchedule")==0) r.status = svc_sync_schedule(cmd->payload, cmd->len);
    else if (strcmp(cmd->op,"wifiScan")==0) r.status = svc_wifi_scan(&r.payload,&r.len);
    else if (strcmp(cmd->op,"wifiConfigure")==0) r.status = svc_wifi_configure(cmd->payload, cmd->len);
    else r.status = -1;
    return r;
}

static void task(void *arg)
{
    cmd_frame_t cmd;
    for(;;){
        if(xQueueReceive(cmdQueue,&cmd,portMAX_DELAY)==pdTRUE){
            ESP_LOGI(TAG, "🎯 CMD_PROC ricevuto comando: id=%u, op=%s, origin=%d", cmd.id, cmd.op, cmd.origin);
            
            resp_frame_t resp = handle(&cmd);
            
            ESP_LOGI(TAG, "🎯 CMD_PROC generata risposta: id=%u, status=%d, payload_size=%zu, origin=%d", 
                     resp.id, resp.status, resp.len, resp.origin);
            
            // Route to correct response queue based on origin
            QueueHandle_t target_queue = NULL;
            const char* transport_name = NULL;
            
            if (resp.origin == ORIGIN_BLE) {
                target_queue = respQueue_BLE;
                transport_name = "BLE";
            } else if (resp.origin == ORIGIN_MQTT) {
                target_queue = respQueue_MQTT;
                transport_name = "MQTT";
            } else {
                ESP_LOGE(TAG, "❌ CMD_PROC unknown origin: %d", resp.origin);
                if (resp.payload) free(resp.payload);
                if (cmd.payload) free(cmd.payload);
                continue;
            }
            
            // Send to correct queue
            BaseType_t queue_result = xQueueSend(target_queue, &resp, 0);
            if (queue_result == pdTRUE) {
                ESP_LOGI(TAG, "✅ CMD_PROC risposta inviata alla queue %s", transport_name);
            } else {
                ESP_LOGE(TAG, "❌ CMD_PROC FAILED to send response to %s queue!", transport_name);
            }
            
            if(cmd.payload) free(cmd.payload);
        }
    }
}

void cmd_proc_start(void){ xTaskCreate(task,"CMD_PROC",8192,NULL,5,NULL); }
