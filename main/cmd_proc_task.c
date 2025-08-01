#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "schedule.h"
#include <string.h>

extern QueueHandle_t cmdQueue;
extern QueueHandle_t respQueue;

static const char *TAG = "CMD_PROC";

static resp_frame_t handle_command(const cmd_frame_t *cmd)
{
    resp_frame_t r = {
        .id       = cmd->id,
        .origin   = cmd->origin,
        .payload  = NULL,
        .len      = 0,
        .is_final = true
    };

    if (strcmp(cmd->op, "syncSchedule") == 0) {
        r.status = svc_sync_schedule(cmd->payload, cmd->len);
    } else {
        r.status = -1;            /* comando sconosciuto */
    }
    return r;
}

static void cmd_proc_task(void *arg)
{
    cmd_frame_t cmd;
    for (;;) {
        if (xQueueReceive(cmdQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "op=%s id=%u", cmd.op, cmd.id);
            resp_frame_t resp = handle_command(&cmd);
            xQueueSend(respQueue, &resp, 0);
            if (cmd.payload) free(cmd.payload);
        }
    }
}

void cmd_proc_start(void)
{
    xTaskCreate(cmd_proc_task, "CMD_PROC", 8192, NULL, 5, NULL);
}
