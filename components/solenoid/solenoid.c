#include "solenoid.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

/* ---------- configurazione pin (matching Kconfig defaults) ---------- */
#define SOL1_GPIO 26
#define SOL2_GPIO 27
#define SOL3_GPIO 14
#define SOL4_GPIO 12

static const uint8_t gpio_map[] = { SOL1_GPIO, SOL2_GPIO, SOL3_GPIO, SOL4_GPIO };
static const size_t  NUM_SOL   = sizeof(gpio_map) / sizeof(gpio_map[0]);
static const char   *TAG       = "SVC_SOLENOID";

/* ---------- helpers ---------- */
static int8_t get_channel_from_json(const uint8_t *json, size_t len, uint8_t *outCh)
{
    if (!json || len == 0) return -2;
    cJSON *root = cJSON_ParseWithLength((const char *)json, len);
    if (!root) return -3;
    const cJSON *ch = cJSON_GetObjectItem(root, "ch");
    if (!cJSON_IsNumber(ch)) { cJSON_Delete(root); return -4; }
    uint8_t chan = (uint8_t) ch->valuedouble;
    cJSON_Delete(root);
    if (chan == 0 || chan > NUM_SOL) return -5;
    *outCh = chan;
    return 0;
}

static void set_gpio(uint8_t chan, bool on)
{
    uint8_t pin = gpio_map[chan - 1];
    gpio_set_level(pin, on ? 1 : 0);
}

/* ---------- API ---------- */
void solenoid_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (size_t i = 0; i < NUM_SOL; ++i) {
        io_conf.pin_bit_mask = 1ULL << gpio_map[i];
        gpio_config(&io_conf);
        gpio_set_level(gpio_map[i], 0);  // tutte OFF
    }
    ESP_LOGI(TAG, "Inizializzate %d valvole", NUM_SOL);
}

int8_t svc_solenoid_on(const uint8_t *json, size_t len)
{
    uint8_t ch; int8_t r = get_channel_from_json(json, len, &ch);
    if (r != 0) return r;
    set_gpio(ch, true);
    ESP_LOGI(TAG, "Solenoid %d ON", ch);
    return 0;
}

int8_t svc_solenoid_off(const uint8_t *json, size_t len)
{
    uint8_t ch; int8_t r = get_channel_from_json(json, len, &ch);
    if (r != 0) return r;
    set_gpio(ch, false);
    ESP_LOGI(TAG, "Solenoid %d OFF", ch);
    return 0;
}
