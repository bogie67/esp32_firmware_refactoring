
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "cmd_frame.h"
#include "resp_frame.h"
#include "schedule.h"
#include "wifi.h"

extern QueueHandle_t cmdQueue;
extern QueueHandle_t respQueue;
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
            resp_frame_t resp = handle(&cmd);
            xQueueSend(respQueue,&resp,0);
            if(cmd.payload) free(cmd.payload);
        }
    }
}

void cmd_proc_start(void){ xTaskCreate(task,"CMD_PROC",8192,NULL,5,NULL); }
