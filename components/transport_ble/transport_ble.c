/* main/transport_ble.c
 * NimBLE transport – SMART_DRIP
 * Compatibile con ESP-IDF v5.4
 */
#include "transport_ble.h"
#include "codec.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "string.h"
#include "nvs_flash.h"

#if CONFIG_MAIN_WITH_BLE
/* NimBLE */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

#define DEVICE_NAME "SMART_DRIP"

/* ──────────────── Queue handles ──────────────── */
static QueueHandle_t cmdQ  = NULL;
static QueueHandle_t respQ = NULL;

#if CONFIG_MAIN_WITH_BLE

/* ──────────────── UUIDs ──────────────── */
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0x00FF);
static const ble_uuid16_t rx_uuid  = BLE_UUID16_INIT(0xFF01);
static const ble_uuid16_t tx_uuid  = BLE_UUID16_INIT(0xFF02);

static uint16_t rx_handle;          /* valore scritto da NimBLE */
static uint16_t tx_handle;          /* valore scritto da NimBLE */

static uint16_t current_conn = BLE_HS_CONN_HANDLE_NONE;



/* ──────────────── GATT callbacks ──────────────── */
static int gatt_chr_access_cb(uint16_t conn_h, uint16_t attr_h,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI("BLE_NIMBLE", "🚨 CALLBACK CHIAMATO! 🚨");
    ESP_LOGI("BLE_NIMBLE", "🔍 GATT Access: conn_handle=%u, attr_handle=0x%04x, op=%d (%s)", 
             conn_h, attr_h, ctxt->op,
             ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR ? "READ_CHR" :
             ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR ? "WRITE_CHR" :
             ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC ? "READ_DSC" :
             ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC ? "WRITE_DSC" :
             "UNKNOWN_OP");

    // Gestione descrittori CCCD
    if (ctxt->chr && ctxt->dsc && ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
            uint16_t cccd_value = 0;
            if (ctxt->om->om_len == 2) {
                cccd_value = get_le16(ctxt->om->om_data);
            }
            ESP_LOGI("BLE_NIMBLE", "📬 CCCD Write: value=0x%04x (%s)", cccd_value,
                     cccd_value & 1 ? "NOTIFY_ENABLED" : "NOTIFY_DISABLED");
            return 0;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
            ESP_LOGI("BLE_NIMBLE", "📫 CCCD Read");
            return 0;
        }
    }
    
    // Gestione scrittura caratteristiche
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        ESP_LOGI("BLE_NIMBLE", "WRITE RX len=%d", om_len);
        ESP_LOGI("BLE_NIMBLE", "📨 Characteristic WRITE: handle=0x%04x, len=%d", attr_h, om_len);
        
        uint8_t *write_data = malloc(om_len);
        if (!write_data) {
            ESP_LOGE("BLE_NIMBLE", "❌ Memory allocation failed");
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        
        ble_hs_mbuf_to_flat(ctxt->om, write_data, om_len, NULL);
        
        cmd_frame_t f;
        if (decode_ble_frame(write_data, om_len, &f)) {
            ESP_LOGI("BLE_NIMBLE", "✅ Frame decodificato: op=%s", f.op);
            f.origin = ORIGIN_BLE;
            xQueueSend(cmdQ, &f, 0);
        } else {
            ESP_LOGW("BLE_NIMBLE", "❌ Errore nella decodifica del frame BLE");
        }
        
        free(write_data);
        return 0;
    }
    
    return BLE_ATT_ERR_UNLIKELY;
}

/* Tabella servizi */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { /* RX */
                .uuid      = &rx_uuid.u,
                .access_cb = gatt_chr_access_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &rx_handle,
            },
            { /* TX */
                .uuid      = &tx_uuid.u,
                .access_cb = gatt_chr_access_cb,         /* solo per read (opz.) */
                .flags     = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                .val_handle = &tx_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16),
                        .access_cb = gatt_chr_access_cb,
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                    },
                    { 0 }
                }
            },
            { 0 }            /* terminatore */
        },
    },
    { 0 }                    /* terminatore */
};

/* ──────────────── GAP helpers ──────────────── */
static void advertise_start(void);

static int gap_evt_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            current_conn = ev->connect.conn_handle;
            ESP_LOGI("BLE_NIMBLE", "Client connesso!");
        } else {
            advertise_start();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        current_conn = BLE_HS_CONN_HANDLE_NONE;
        advertise_start();
        break;

    default:
        break;
    }
    return 0;
}

static void advertise_start(void)
{
    struct ble_gap_adv_params adv = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv,
                      gap_evt_cb, NULL);
}

/* ──────────────── TX task ──────────────── */
static void notify_resp(const resp_frame_t *r)
{
    if (current_conn == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW("BLE_NIMBLE", "❌ No client connected - skipping notify");
        return;
    }

    size_t len;
    uint8_t *buf = encode_ble_resp(r, &len);
    if (!buf) {
        ESP_LOGE("BLE_NIMBLE", "❌ Failed to encode response");
        return;
    }

    ESP_LOGI("BLE_NIMBLE", "📤 Sending notify: conn=%d, handle=%d, len=%zu", 
             current_conn, tx_handle, len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    free(buf);
    int rc = ble_gattc_notify_custom(current_conn, tx_handle, om);
    
    if (rc == 0) {
        ESP_LOGI("BLE_NIMBLE", "✅ Notifica inviata con successo");
    } else {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore invio notifica: %d", rc);
    }
}

static void tx_task(void *arg)
{
    resp_frame_t resp;
    for (;;) {
        if (xQueueReceive(respQ, &resp, portMAX_DELAY) == pdTRUE) {
            if (resp.origin == ORIGIN_BLE) notify_resp(&resp);
            if (resp.is_final && resp.payload) free(resp.payload);
        }
    }
}

/* ──────────────── Callback di sincronizzazione ──────────────── */
static void on_sync(void)
{
    ESP_LOGI("BLE_NIMBLE", "NimBLE sincronizzato - avvio advertising");
    advertise_start();
}

static void on_reset(int reason)
{
    ESP_LOGW("BLE_NIMBLE", "NimBLE reset, reason=%d", reason);
}

/* ──────────────── NimBLE host thread ──────────────── */
static void host_task(void *param)
{
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    
    /* Avvia i servizi GATT - FONDAMENTALE! */
    ESP_ERROR_CHECK(ble_gatts_start());
    
    /* Log di debug: quali handle sono stati assegnati? */
    ESP_LOGI("BLE_NIMBLE", "✅ GATT services started!");
    ESP_LOGI("BLE_NIMBLE", "RX handle = 0x%04x", rx_handle);
    ESP_LOGI("BLE_NIMBLE", "TX handle = 0x%04x", tx_handle);

    /* Loop NimBLE host */
    ESP_LOGI("BLE_NIMBLE", "Starting NimBLE host loop...");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

#endif /* CONFIG_MAIN_WITH_BLE */

/* ──────────────── API pubblica ──────────────── */
void smart_ble_transport_init(QueueHandle_t cQ, QueueHandle_t rQ)
{
#if CONFIG_MAIN_WITH_BLE
    cmdQ  = cQ;
    respQ = rQ;

    ESP_ERROR_CHECK(nvs_flash_init());
    nimble_port_init();
    
    /* Registra i callbacks */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    
    nimble_port_freertos_init(host_task);

    xTaskCreate(tx_task, "ble_tx_task", 4096, NULL, 5, NULL);

    ESP_LOGI("BLE_NIMBLE", "Transport inizializzato (NimBLE)");
#else
    /* Stub per i test Unity */
    ESP_LOGI("BLE_NIMBLE", "BLE disabled for testing - stub implementation");
    (void)cQ;
    (void)rQ;
#endif
}
