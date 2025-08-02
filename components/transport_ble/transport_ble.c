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

/* ──────────────── State management ──────────────── */
static ble_state_t ble_state = BLE_DOWN;
static QueueHandle_t cmd_queue = NULL;
static QueueHandle_t resp_queue = NULL;

/* ──────────────── Connection tracking ──────────────── */
static uint16_t current_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t negotiated_mtu = 23;  // Default BLE ATT MTU
static TaskHandle_t tx_task_handle = NULL;

/* ──────────────── Chunking configuration ──────────────── */
static ble_chunk_config_t chunk_config = {
    .max_chunk_size = 20,      // Will be updated after MTU negotiation
    .max_concurrent = 4,
    .reassembly_timeout_ms = 2000
};

#if CONFIG_MAIN_WITH_BLE

/* ──────────────── UUIDs ──────────────── */
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0x00FF);
static const ble_uuid16_t rx_uuid  = BLE_UUID16_INIT(0xFF01);
static const ble_uuid16_t tx_uuid  = BLE_UUID16_INIT(0xFF02);

static uint16_t rx_handle;          /* valore scritto da NimBLE */
static uint16_t tx_handle;          /* valore scritto da NimBLE */



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
            xQueueSend(cmd_queue, &f, 0);
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
            ble_state = BLE_UP;
            ESP_LOGI("BLE_NIMBLE", "✅ Client connesso - conn_handle=%u", current_conn);
            
            // Avvia MTU exchange per ottimizzare chunking
            ble_gattc_exchange_mtu(current_conn, NULL, NULL);
        } else {
            ESP_LOGW("BLE_NIMBLE", "❌ Connessione fallita: status=%d", ev->connect.status);
            ble_state = BLE_ADVERTISING;
            advertise_start();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("BLE_NIMBLE", "📱 Client disconnesso - reason=%d", ev->disconnect.reason);
        current_conn = BLE_HS_CONN_HANDLE_NONE;
        negotiated_mtu = 23;  // Reset to default
        ble_state = BLE_ADVERTISING;
        advertise_start();
        break;
        
    case BLE_GAP_EVENT_MTU:
        negotiated_mtu = ev->mtu.value;
        chunk_config.max_chunk_size = negotiated_mtu - 3;  // ATT header overhead
        ESP_LOGI("BLE_NIMBLE", "📏 MTU negoziato: %u bytes, chunk_size: %u", 
                 negotiated_mtu, chunk_config.max_chunk_size);
        break;

    default:
        ESP_LOGV("BLE_NIMBLE", "🔄 GAP event: %d", ev->type);
        break;
    }
    return 0;
}

static void advertise_start(void)
{
    if (ble_state != BLE_ADVERTISING && ble_state != BLE_STARTING) {
        ESP_LOGD("BLE_NIMBLE", "⏭️ Skip advertising - stato: %d", ble_state);
        return;
    }
    
    struct ble_gap_adv_params adv = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 32,    // 20ms (units of 0.625ms)
        .itvl_max = 160,   // 100ms for balance of discoverability vs power
    };
    
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore impostazione adv fields: %d", rc);
        ble_state = BLE_ERROR;
        return;
    }
    
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv,
                          gap_evt_cb, NULL);
    if (rc != 0) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore avvio advertising: %d", rc);
        ble_state = BLE_ERROR;
        return;
    }
    
    ble_state = BLE_ADVERTISING;
    ESP_LOGI("BLE_NIMBLE", "📡 Advertising avviato - device: %s", DEVICE_NAME);
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
    
    ESP_LOGI("BLE_NIMBLE", "🚀 BLE TX task avviato");
    
    for (;;) {
        if (xQueueReceive(resp_queue, &resp, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD("BLE_NIMBLE", "📤 Ricevuta risposta per origin %d", resp.origin);
            
            // Invia solo risposte con origin BLE
            if (resp.origin != ORIGIN_BLE) {
                ESP_LOGV("BLE_NIMBLE", "⏭️ Risposta non per BLE, saltando");
                continue;
            }
            
            // Controlla stato BLE prima di inviare
            if (ble_state != BLE_UP) {
                ESP_LOGW("BLE_NIMBLE", "⚠️ BLE down - scartando risposta id=%u", resp.id);
                if (resp.is_final && resp.payload) {
                    free(resp.payload);
                }
                continue;
            }
            
            notify_resp(&resp);
            
            // Cleanup payload se finale
            if (resp.is_final && resp.payload) {
                free(resp.payload);
            }
        }
    }
}

/* ──────────────── Callback di sincronizzazione ──────────────── */
static void on_sync(void)
{
    ESP_LOGI("BLE_NIMBLE", "✅ NimBLE sincronizzato - avvio advertising");
    ble_state = BLE_STARTING;
    advertise_start();
}

static void on_reset(int reason)
{
    ESP_LOGW("BLE_NIMBLE", "🔄 NimBLE reset, reason=%d", reason);
    ble_state = BLE_ERROR;
    current_conn = BLE_HS_CONN_HANDLE_NONE;
    negotiated_mtu = 23;
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

void transport_ble_init(QueueHandle_t cmdQueue, QueueHandle_t respQueue)
{
    ESP_LOGI("BLE_NIMBLE", "🏗️ Inizializzazione transport BLE");
    
    if (!cmdQueue || !respQueue) {
        ESP_LOGE("BLE_NIMBLE", "❌ Queue non valide");
        return;
    }
    
    cmd_queue = cmdQueue;
    resp_queue = respQueue;
    ble_state = BLE_DOWN;
    current_conn = BLE_HS_CONN_HANDLE_NONE;
    negotiated_mtu = 23;
    
    ESP_LOGI("BLE_NIMBLE", "✅ Transport BLE inizializzato");
}

void transport_ble_start(void)
{
#if CONFIG_MAIN_WITH_BLE
    ESP_LOGI("BLE_NIMBLE", "🚀 Avvio transport BLE");
    
    if (ble_state != BLE_DOWN) {
        ESP_LOGW("BLE_NIMBLE", "⚠️ BLE già avviato, stato: %d", ble_state);
        return;
    }
    
    ble_state = BLE_STARTING;
    
    ESP_ERROR_CHECK(nvs_flash_init());
    nimble_port_init();
    
    /* Registra i callbacks */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    
    nimble_port_freertos_init(host_task);

    // Crea task TX per risposte
    BaseType_t result = xTaskCreate(
        tx_task,
        "BLE_TX",
        4096,
        NULL,
        5,
        &tx_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore creazione task TX");
        ble_state = BLE_ERROR;
        return;
    }

    ESP_LOGI("BLE_NIMBLE", "✅ Transport BLE avviato");
#else
    /* Stub per i test Unity */
    ESP_LOGI("BLE_NIMBLE", "BLE disabled for testing - stub implementation");
    ble_state = BLE_UP;  // Simula stato connesso per test
#endif
}

void transport_ble_stop(void)
{
    ESP_LOGI("BLE_NIMBLE", "🛑 Arresto transport BLE");
    
#if CONFIG_MAIN_WITH_BLE
    // Ferma advertising se attivo
    if (ble_state == BLE_ADVERTISING || ble_state == BLE_UP) {
        ble_gap_adv_stop();
    }
    
    // Disconnetti client se connesso
    if (current_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(current_conn, BLE_ERR_REM_USER_CONN_TERM);
        current_conn = BLE_HS_CONN_HANDLE_NONE;
    }
#endif
    
    ble_state = BLE_DOWN;
    negotiated_mtu = 23;
    
    ESP_LOGI("BLE_NIMBLE", "✅ Transport BLE arrestato");
}

bool transport_ble_is_connected(void)
{
    return (ble_state == BLE_UP);
}

ble_state_t transport_ble_get_state(void)
{
    return ble_state;
}

void transport_ble_cleanup(void)
{
    ESP_LOGI("BLE_NIMBLE", "🧹 Cleanup transport BLE");
    
    transport_ble_stop();
    
#if CONFIG_MAIN_WITH_BLE
    // Ferma task TX
    if (tx_task_handle) {
        vTaskDelete(tx_task_handle);
        tx_task_handle = NULL;
    }
    
    // Cleanup NimBLE stack
    if (ble_state != BLE_DOWN) {
        nimble_port_stop();
        nimble_port_deinit();
    }
#endif
    
    // Reset stato
    cmd_queue = NULL;
    resp_queue = NULL;
    ble_state = BLE_DOWN;
    current_conn = BLE_HS_CONN_HANDLE_NONE;
    negotiated_mtu = 23;
    
    ESP_LOGI("BLE_NIMBLE", "✅ Cleanup transport BLE completato");
}

esp_err_t transport_ble_set_chunk_config(const ble_chunk_config_t *config)
{
    if (!config) {
        // Reset to defaults
        chunk_config.max_chunk_size = (negotiated_mtu > 23) ? negotiated_mtu - 3 : 20;
        chunk_config.max_concurrent = 4;
        chunk_config.reassembly_timeout_ms = 2000;
    } else {
        chunk_config = *config;
    }
    
    ESP_LOGI("BLE_NIMBLE", "📏 Chunk config: size=%u, concurrent=%u, timeout=%lu ms",
             chunk_config.max_chunk_size, chunk_config.max_concurrent, 
             chunk_config.reassembly_timeout_ms);
             
    return ESP_OK;
}

esp_err_t transport_ble_get_connection_info(uint16_t *conn_handle, 
                                          uint16_t *mtu, 
                                          uint8_t *chunks_pending)
{
    if (ble_state != BLE_UP) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (conn_handle) *conn_handle = current_conn;
    if (mtu) *mtu = negotiated_mtu;
    if (chunks_pending) *chunks_pending = 0;  // TODO: implement chunking tracking
    
    return ESP_OK;
}

/* Legacy API - backward compatibility */
void smart_ble_transport_init(QueueHandle_t cQ, QueueHandle_t rQ)
{
    transport_ble_init(cQ, rQ);
    transport_ble_start();  // Auto-start for legacy compatibility
}
