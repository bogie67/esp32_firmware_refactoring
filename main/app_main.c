
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "cmd_frame.h"
#include "resp_frame.h"
#include "transport_ble.h"
#include "cmd_proc.h"

static const char *TAG = "APP_MAIN";
QueueHandle_t cmdQueue;
QueueHandle_t respQueue;

static void cmd_proc_task(void *arg)
{
    cmd_frame_t cmd;
    for (;;) {
        if (xQueueReceive(cmdQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "CMD id=%u op=%s len=%u", cmd.id, cmd.op, cmd.len);
            resp_frame_t resp = {
                .id = cmd.id,
                .status = 0,
                .payload = cmd.payload,
                .len = cmd.len,
                .origin = ORIGIN_BLE,
                .is_final = true
            };
            xQueueSend(respQueue, &resp, 0);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting NimBLE demo");

    cmdQueue = xQueueCreate(10, sizeof(cmd_frame_t));
    respQueue = xQueueCreate(10, sizeof(resp_frame_t));

    xTaskCreate(cmd_proc_task, "CMD_PROC", 4096, NULL, 5, NULL);

    cmd_proc_start();

    smart_ble_transport_init(cmdQueue, respQueue);
}
