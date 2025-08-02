/* main/transport_ble.c
 * NimBLE transport – SMART_DRIP
 * Compatibile con ESP-IDF v5.4
 */
#include "transport_ble.h"
#include "codec.h"
#include "chunk_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/task.h"
#include "string.h"
#include "nvs_flash.h"
#include <inttypes.h>

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

/* ──────────────── Back-off & resilience ──────────────── */
static esp_timer_handle_t advertising_timer = NULL;
static uint32_t advertising_backoff_ms = 1000;  // Initial: 1 second
static const uint32_t ADVERTISING_BACKOFF_MAX_MS = 32000;  // Max: 32 seconds
static const uint32_t ADVERTISING_BACKOFF_INITIAL_MS = 1000;  // Reset value

/* ──────────────── Chunking integration ──────────────── */
static bool chunk_manager_initialized = false;

#if CONFIG_MAIN_WITH_BLE

/* ──────────────── Forward declarations ──────────────── */
static void advertise_start(void);

/* ──────────────── Back-off advertising helpers ──────────────── */

/**
 * @brief Schedula re-advertising con back-off exponential + jitter
 */
static void schedule_advertising_backoff(void)
{
    if (ble_state != BLE_ADVERTISING) {
        ESP_LOGD("BLE_NIMBLE", "⏭️ Skip back-off scheduling - stato: %d", ble_state);
        return;
    }
    
    if (esp_timer_is_active(advertising_timer)) {
        ESP_LOGD("BLE_NIMBLE", "⏰ Timer advertising già attivo");
        return;  // Timer già schedulato
    }
    
    // Jitter ±10% per evitare thundering herd
    uint32_t jitter = esp_random() % (advertising_backoff_ms / 10);
    uint32_t total_delay = advertising_backoff_ms + jitter;
    
    ESP_LOGW("BLE_NIMBLE", "📡 Re-advertising in %" PRIu32 " ms (backoff: %" PRIu32 " + jitter: %" PRIu32 ")", 
             total_delay, advertising_backoff_ms, jitter);
    
    // Avvia timer one-shot
    esp_err_t err = esp_timer_start_once(advertising_timer, total_delay * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore avvio timer advertising: %s", esp_err_to_name(err));
        return;
    }
    
    // Raddoppia back-off per prossimo tentativo (con limite massimo)
    advertising_backoff_ms = (advertising_backoff_ms * 2 > ADVERTISING_BACKOFF_MAX_MS) 
                           ? ADVERTISING_BACKOFF_MAX_MS 
                           : advertising_backoff_ms * 2;
}

/**
 * @brief Callback timer per re-advertising
 */
static void advertising_timer_callback(void *arg)
{
    ESP_LOGI("BLE_NIMBLE", "🔄 Timer advertising scaduto - riavvio advertising");
    
    if (ble_state == BLE_ADVERTISING) {
        // Riavvia advertising (advertise_start gestisce già lo stato)
        advertise_start();
    }
}

/**
 * @brief Reset back-off su connessione riuscita
 */
static void reset_advertising_backoff(void)
{
    // Ferma timer se attivo
    if (advertising_timer && esp_timer_is_active(advertising_timer)) {
        esp_timer_stop(advertising_timer);
        ESP_LOGD("BLE_NIMBLE", "⏰ Timer advertising fermato");
    }
    
    // Reset back-off a valore iniziale
    advertising_backoff_ms = ADVERTISING_BACKOFF_INITIAL_MS;
    ESP_LOGD("BLE_NIMBLE", "🔄 Back-off advertising reset a %" PRIu32 " ms", advertising_backoff_ms);
}

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
        ESP_LOGD("BLE_NIMBLE", "📨 Characteristic WRITE: handle=0x%04x, len=%d", attr_h, om_len);
        
        uint8_t *write_data = malloc(om_len);
        if (!write_data) {
            ESP_LOGE("BLE_NIMBLE", "❌ Memory allocation failed");
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        
        ble_hs_mbuf_to_flat(ctxt->om, write_data, om_len, NULL);
        
        // Check if this might be a chunk
        if (chunk_manager_initialized && om_len >= sizeof(chunk_header_t)) {
            const chunk_header_t *header = (const chunk_header_t*)write_data;
            
            ESP_LOGD("BLE_NIMBLE", "🔍 Frame analysis: len=%d, flags=0x%02x, chunk_idx=%u, total_chunks=%u, frame_id=%u",
                     om_len, header->flags, header->chunk_idx, header->total_chunks, header->frame_id);
            
            // More robust chunk detection: check multiple header fields
            bool is_chunk = (header->flags & CHUNK_FLAG_CHUNKED) && 
                           (header->chunk_idx < 8) &&  // Valid chunk index
                           (header->total_chunks > 0 && header->total_chunks <= 8) &&  // Valid total chunks
                           (header->frame_id != 0) &&  // Valid frame ID
                           (header->chunk_size <= (chunk_config.max_chunk_size - sizeof(chunk_header_t)));  // Valid chunk size
            
            ESP_LOGD("BLE_NIMBLE", "🔍 Chunk detection result: %s", is_chunk ? "CHUNK" : "DIRECT_FRAME");
            
            if (is_chunk) {
                ESP_LOGD("BLE_NIMBLE", "📦 Received chunk %u/%u for frame %u", 
                         header->chunk_idx + 1, header->total_chunks, header->frame_id);
                
                reassembly_result_t reassembly_result;
                esp_err_t err = chunk_manager_process(write_data, om_len, &reassembly_result);
                
                if (err == ESP_OK) {
                    if (reassembly_result.is_complete) {
                        ESP_LOGI("BLE_NIMBLE", "✅ Frame %u completed via chunking, size: %zu", 
                                 reassembly_result.frame_id, reassembly_result.frame_size);
                        
                        // Decode the complete frame
                        cmd_frame_t f;
                        if (decode_ble_frame(reassembly_result.complete_frame, reassembly_result.frame_size, &f)) {
                            f.origin = ORIGIN_BLE;
                            xQueueSend(cmd_queue, &f, 0);
                            ESP_LOGI("BLE_NIMBLE", "✅ Chunked frame decoded: op=%s", f.op);
                        } else {
                            ESP_LOGE("BLE_NIMBLE", "❌ Failed to decode complete chunked frame");
                        }
                        
                        free(reassembly_result.complete_frame);
                    } else if (reassembly_result.is_duplicate) {
                        ESP_LOGD("BLE_NIMBLE", "🔄 Duplicate chunk ignored");
                    } else {
                        ESP_LOGD("BLE_NIMBLE", "📝 Chunk stored, waiting for more");
                    }
                } else {
                    ESP_LOGE("BLE_NIMBLE", "❌ Chunk processing failed: %s", esp_err_to_name(err));
                }
                
                free(write_data);
                return 0;
            }
        }
        
        // Try direct frame decode (not chunked or chunking disabled)
        cmd_frame_t f;
        if (decode_ble_frame(write_data, om_len, &f)) {
            ESP_LOGI("BLE_NIMBLE", "✅ Direct frame decoded: op=%s", f.op);
            f.origin = ORIGIN_BLE;
            xQueueSend(cmd_queue, &f, 0);
        } else {
            ESP_LOGW("BLE_NIMBLE", "❌ Failed to decode frame (len=%d)", om_len);
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

static int gap_evt_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            current_conn = ev->connect.conn_handle;
            ble_state = BLE_UP;
            ESP_LOGI("BLE_NIMBLE", "✅ Client connesso - conn_handle=%u", current_conn);
            
            // Reset back-off su connessione riuscita
            reset_advertising_backoff();
            
            // Avvia MTU exchange per ottimizzare chunking
            ble_gattc_exchange_mtu(current_conn, NULL, NULL);
        } else {
            ESP_LOGW("BLE_NIMBLE", "❌ Connessione fallita: status=%d", ev->connect.status);
            ble_state = BLE_ADVERTISING;
            // Schedula re-advertising con back-off
            schedule_advertising_backoff();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("BLE_NIMBLE", "📱 Client disconnesso - reason=%d", ev->disconnect.reason);
        current_conn = BLE_HS_CONN_HANDLE_NONE;
        negotiated_mtu = 23;  // Reset to default
        ble_state = BLE_ADVERTISING;
        
        // Riavvia advertising immediatamente dopo disconnessione
        advertise_start();
        break;
        
    case BLE_GAP_EVENT_MTU:
        negotiated_mtu = ev->mtu.value;
        chunk_config.max_chunk_size = negotiated_mtu - 3;  // ATT header overhead
        
        // Update chunk manager with new MTU
        if (chunk_manager_initialized) {
            chunk_manager_deinit();
            
            chunk_config_t chunk_cfg = {
                .max_chunk_size = negotiated_mtu - 3,
                .header_size = sizeof(chunk_header_t),
                .max_concurrent_frames = chunk_config.max_concurrent,
                .reassembly_timeout_ms = chunk_config.reassembly_timeout_ms
            };
            
            esp_err_t err = chunk_manager_init(&chunk_cfg);
            if (err != ESP_OK) {
                ESP_LOGW("BLE_NIMBLE", "⚠️ Failed to reinit chunk manager with new MTU");
                chunk_manager_initialized = false;
            }
        }
        
        ESP_LOGI("BLE_NIMBLE", "📏 MTU negoziato: %u bytes, chunk_size: %u", 
                 negotiated_mtu, chunk_config.max_chunk_size);
        break;
        
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (ble_state == BLE_ADVERTISING) {
            ESP_LOGD("BLE_NIMBLE", "📡 Advertising completato - schedulo back-off");
            // Advertising terminato senza connessioni, schedula back-off
            schedule_advertising_backoff();
        }
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
    
    // Ferma advertising esistente prima di riavviare
    ble_gap_adv_stop();
    
    // Parametri advertising adattivi basati su back-off
    uint16_t adv_interval_min, adv_interval_max;
    uint32_t adv_duration_ms;
    
    if (advertising_backoff_ms <= ADVERTISING_BACKOFF_INITIAL_MS) {
        // Fast advertising iniziale
        adv_interval_min = 32;   // 20ms
        adv_interval_max = 80;   // 50ms  
        adv_duration_ms = 30000; // 30 secondi
    } else {
        // Slow advertising dopo back-off
        adv_interval_min = 160;  // 100ms
        adv_interval_max = 480;  // 300ms
        adv_duration_ms = 10000; // 10 secondi
    }
    
    struct ble_gap_adv_params adv = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = adv_interval_min,
        .itvl_max = adv_interval_max,
    };
    
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore impostazione adv fields: %d", rc);
        ble_state = BLE_ERROR;
        schedule_advertising_backoff();
        return;
    }
    
    // Advertising con timeout limitato per attivare back-off
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, adv_duration_ms * 1000, &adv,
                          gap_evt_cb, NULL);
    if (rc != 0) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore avvio advertising: %d", rc);
        ble_state = BLE_ERROR;
        schedule_advertising_backoff();
        return;
    }
    
    ble_state = BLE_ADVERTISING;
    ESP_LOGI("BLE_NIMBLE", "📡 Advertising avviato - device: %s, interval: %u-%ums, duration: %lums", 
             DEVICE_NAME, adv_interval_min * 625 / 1000, adv_interval_max * 625 / 1000, adv_duration_ms);
}

/* ──────────────── TX task ──────────────── */
static void notify_resp(const resp_frame_t *r)
{
    ESP_LOGI("BLE_NIMBLE", "🔔 notify_resp called: id=%u, payload_size=%zu", 
             r->id, r->payload ? strlen((char*)r->payload) : 0);
             
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

    ESP_LOGD("BLE_NIMBLE", "📤 Sending response: conn=%d, handle=%d, len=%zu", 
             current_conn, tx_handle, len);

    // Check if chunking is needed
    if (chunk_manager_initialized && len > (negotiated_mtu - 3)) {
        // Use chunking for large responses
        chunk_result_t chunk_result;
        esp_err_t err = chunk_manager_send(buf, len, &chunk_result);
        if (err == ESP_OK) {
            ESP_LOGI("BLE_NIMBLE", "📦 Response chunked into %u parts, frame_id=%u", 
                     chunk_result.chunk_count, chunk_result.frame_id);
            
            // Send all chunks
            for (uint8_t i = 0; i < chunk_result.chunk_count; i++) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk_result.chunks[i], chunk_result.chunk_sizes[i]);
                if (om) {
                    int rc = ble_gattc_notify_custom(current_conn, tx_handle, om);
                    if (rc == 0) {
                        ESP_LOGD("BLE_NIMBLE", "✅ Chunk %u/%u sent successfully", 
                                 i + 1, chunk_result.chunk_count);
                    } else {
                        ESP_LOGE("BLE_NIMBLE", "❌ Failed to send chunk %u: %d", i, rc);
                        break;  // Stop on first error
                    }
                    
                    // Small delay between chunks to avoid mbuf exhaustion
                    vTaskDelay(pdMS_TO_TICKS(10));
                } else {
                    ESP_LOGE("BLE_NIMBLE", "❌ Failed to create mbuf for chunk %u", i);
                    break;
                }
            }
            
            chunk_manager_free_send_result(&chunk_result);
        } else {
            ESP_LOGE("BLE_NIMBLE", "❌ Chunking failed: %s", esp_err_to_name(err));
            // Fallback to direct send (will likely fail but try anyway)
            struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
            if (om) {
                ble_gattc_notify_custom(current_conn, tx_handle, om);
            }
        }
    } else {
        // Direct send for small responses
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        if (om) {
            int rc = ble_gattc_notify_custom(current_conn, tx_handle, om);
            if (rc == 0) {
                ESP_LOGI("BLE_NIMBLE", "✅ Direct notify sent successfully, len=%zu", len);
            } else {
                ESP_LOGE("BLE_NIMBLE", "❌ Direct notify failed: %d", rc);
            }
        } else {
            ESP_LOGE("BLE_NIMBLE", "❌ Failed to create mbuf for direct send");
        }
    }

    free(buf);
}

static void tx_task(void *arg)
{
    resp_frame_t resp;
    
    ESP_LOGI("BLE_NIMBLE", "🚀 BLE TX task avviato");
    
    for (;;) {
        if (xQueueReceive(resp_queue, &resp, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI("BLE_NIMBLE", "📤 TX task ricevuta risposta: id=%u, origin=%d, payload_size=%zu", 
                     resp.id, resp.origin, resp.payload ? strlen((char*)resp.payload) : 0);
            
            // Invia solo risposte con origin BLE
            if (resp.origin != ORIGIN_BLE) {
                ESP_LOGW("BLE_NIMBLE", "⏭️ Risposta con origin %d diverso da BLE (%d), saltando", resp.origin, ORIGIN_BLE);
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

    // Initialize chunk manager
    chunk_config_t chunk_cfg = {
        .max_chunk_size = negotiated_mtu - 3,  // Will be updated after MTU negotiation
        .header_size = sizeof(chunk_header_t),
        .max_concurrent_frames = chunk_config.max_concurrent,
        .reassembly_timeout_ms = chunk_config.reassembly_timeout_ms
    };
    
    esp_err_t err = chunk_manager_init(&chunk_cfg);
    if (err == ESP_OK) {
        chunk_manager_initialized = true;
        ESP_LOGI("BLE_NIMBLE", "✅ Chunk manager initialized - max_chunk: %u", chunk_cfg.max_chunk_size);
    } else {
        ESP_LOGW("BLE_NIMBLE", "⚠️ Chunk manager init failed: %s", esp_err_to_name(err));
        chunk_manager_initialized = false;
    }

    // Crea timer per back-off advertising
    const esp_timer_create_args_t timer_args = {
        .callback = &advertising_timer_callback,
        .name = "ble_adv_backoff"
    };
    err = esp_timer_create(&timer_args, &advertising_timer);
    if (err != ESP_OK) {
        ESP_LOGE("BLE_NIMBLE", "❌ Errore creazione timer advertising: %s", esp_err_to_name(err));
        ble_state = BLE_ERROR;
        return;
    }

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

    ESP_LOGI("BLE_NIMBLE", "✅ Transport BLE avviato - back-off: %" PRIu32 "-%" PRIu32 " ms", 
             ADVERTISING_BACKOFF_INITIAL_MS, ADVERTISING_BACKOFF_MAX_MS);
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
    // Ferma timer advertising se attivo
    if (advertising_timer && esp_timer_is_active(advertising_timer)) {
        esp_timer_stop(advertising_timer);
        ESP_LOGD("BLE_NIMBLE", "⏰ Timer advertising fermato");
    }
    
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
    advertising_backoff_ms = ADVERTISING_BACKOFF_INITIAL_MS;
    
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
    
    // Cleanup chunk manager
    if (chunk_manager_initialized) {
        chunk_manager_deinit();
        chunk_manager_initialized = false;
    }
    
#if CONFIG_MAIN_WITH_BLE
    // Cleanup timer advertising
    if (advertising_timer) {
        esp_timer_delete(advertising_timer);
        advertising_timer = NULL;
    }
    
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
    advertising_backoff_ms = ADVERTISING_BACKOFF_INITIAL_MS;
    
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
    
    // Update chunk manager if initialized
    if (chunk_manager_initialized) {
        chunk_manager_deinit();
        
        chunk_config_t chunk_cfg = {
            .max_chunk_size = chunk_config.max_chunk_size,
            .header_size = sizeof(chunk_header_t),
            .max_concurrent_frames = chunk_config.max_concurrent,
            .reassembly_timeout_ms = chunk_config.reassembly_timeout_ms
        };
        
        esp_err_t err = chunk_manager_init(&chunk_cfg);
        if (err != ESP_OK) {
            ESP_LOGE("BLE_NIMBLE", "❌ Failed to reinit chunk manager: %s", esp_err_to_name(err));
            chunk_manager_initialized = false;
            return err;
        }
    }
    
    ESP_LOGI("BLE_NIMBLE", "📏 Chunk config updated: size=%u, concurrent=%u, timeout=%lu ms",
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
    
    if (chunks_pending) {
        if (chunk_manager_initialized) {
            uint8_t active_contexts;
            chunk_manager_get_stats(&active_contexts, NULL, NULL, NULL);
            *chunks_pending = active_contexts;
        } else {
            *chunks_pending = 0;
        }
    }
    
    return ESP_OK;
}

/* Legacy API - backward compatibility */
void smart_ble_transport_init(QueueHandle_t cQ, QueueHandle_t rQ)
{
    transport_ble_init(cQ, rQ);
    transport_ble_start();  // Auto-start for legacy compatibility
}
