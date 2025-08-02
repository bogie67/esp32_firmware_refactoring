#include "chunk_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CHUNK_MGR";

/**
 * @brief Reassembly context for frame reconstruction
 */
typedef struct {
    uint16_t frame_id;           ///< Frame identifier
    uint32_t timestamp_ms;       ///< Creation timestamp for timeout
    uint8_t chunks_received;     ///< Bitmap of received chunks (max 8 chunks per frame)
    uint8_t total_chunks;        ///< Expected total chunks
    size_t expected_size;        ///< Expected total frame size
    size_t current_size;         ///< Current accumulated size
    uint8_t *buffer;             ///< Reassembly buffer
    bool active;                 ///< Context is in use
} reassembly_context_t;

/* ──────────────── Static variables ──────────────── */
static chunk_config_t config;
static bool initialized = false;
static reassembly_context_t *contexts = NULL;
static uint16_t next_frame_id = 1;
static SemaphoreHandle_t chunk_mutex = NULL;

/* Statistics */
static uint32_t stats_frames_sent = 0;
static uint32_t stats_frames_received = 0;
static uint32_t stats_timeouts = 0;

/* ──────────────── Helper functions ──────────────── */

/**
 * @brief Get current timestamp in milliseconds
 */
static uint32_t get_timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Find available reassembly context
 */
static reassembly_context_t* find_free_context(void)
{
    for (int i = 0; i < config.max_concurrent_frames; i++) {
        if (!contexts[i].active) {
            return &contexts[i];
        }
    }
    return NULL;
}

/**
 * @brief Find context by frame ID
 */
static reassembly_context_t* find_context_by_id(uint16_t frame_id)
{
    for (int i = 0; i < config.max_concurrent_frames; i++) {
        if (contexts[i].active && contexts[i].frame_id == frame_id) {
            return &contexts[i];
        }
    }
    return NULL;
}

/**
 * @brief Free reassembly context
 */
static void free_context(reassembly_context_t *ctx)
{
    if (ctx && ctx->active) {
        if (ctx->buffer) {
            free(ctx->buffer);
        }
        memset(ctx, 0, sizeof(reassembly_context_t));
        ctx->active = false;
    }
}

/**
 * @brief Calculate number of chunks needed for data
 */
static uint8_t calculate_chunks_needed(size_t data_size)
{
    size_t effective_chunk_size = config.max_chunk_size - sizeof(chunk_header_t);
    uint8_t chunks = (data_size + effective_chunk_size - 1) / effective_chunk_size;
    return chunks > 0 ? chunks : 1;
}

/* ──────────────── Public API ──────────────── */

esp_err_t chunk_manager_init(const chunk_config_t *cfg)
{
    if (!cfg) {
        ESP_LOGE(TAG, "❌ Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (initialized) {
        ESP_LOGW(TAG, "⚠️ Chunk manager already initialized");
        return ESP_OK;
    }
    
    // Copy configuration
    config = *cfg;
    
    // Validate configuration
    if (config.max_chunk_size < sizeof(chunk_header_t) + 1) {
        ESP_LOGE(TAG, "❌ max_chunk_size too small: %u", config.max_chunk_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config.max_concurrent_frames == 0 || config.max_concurrent_frames > 8) {
        ESP_LOGE(TAG, "❌ max_concurrent_frames invalid: %u", config.max_concurrent_frames);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate contexts
    contexts = calloc(config.max_concurrent_frames, sizeof(reassembly_context_t));
    if (!contexts) {
        ESP_LOGE(TAG, "❌ Failed to allocate reassembly contexts");
        return ESP_ERR_NO_MEM;
    }
    
    // Create mutex
    chunk_mutex = xSemaphoreCreateMutex();
    if (!chunk_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create chunk mutex");
        free(contexts);
        contexts = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    initialized = true;
    
    ESP_LOGI(TAG, "✅ Chunk manager initialized - max_chunk: %u, concurrent: %u, timeout: %lums",
             config.max_chunk_size, config.max_concurrent_frames, config.reassembly_timeout_ms);
             
    return ESP_OK;
}

esp_err_t chunk_manager_send(const uint8_t *data, size_t data_size, chunk_result_t *result)
{
    if (!initialized || !data || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(result, 0, sizeof(chunk_result_t));
    
    // Calculate chunks needed
    uint8_t chunks_needed = calculate_chunks_needed(data_size);
    size_t effective_chunk_size = config.max_chunk_size - sizeof(chunk_header_t);
    
    if (chunks_needed > 8) {
        ESP_LOGE(TAG, "❌ Frame too large, needs %u chunks (max 8)", chunks_needed);
        return ESP_ERR_INVALID_SIZE;
    }
    
    xSemaphoreTake(chunk_mutex, portMAX_DELAY);
    
    // Assign frame ID
    uint16_t frame_id = next_frame_id++;
    if (next_frame_id == 0) next_frame_id = 1; // Avoid ID 0
    
    xSemaphoreGive(chunk_mutex);
    
    // Allocate result arrays
    result->chunks = malloc(chunks_needed * sizeof(uint8_t*));
    result->chunk_sizes = malloc(chunks_needed * sizeof(size_t));
    if (!result->chunks || !result->chunk_sizes) {
        ESP_LOGE(TAG, "❌ Failed to allocate chunk arrays");
        chunk_manager_free_send_result(result);
        return ESP_ERR_NO_MEM;
    }
    
    // Create chunks
    size_t offset = 0;
    for (uint8_t i = 0; i < chunks_needed; i++) {
        size_t remaining = data_size - offset;
        size_t chunk_payload_size = (remaining > effective_chunk_size) ? effective_chunk_size : remaining;
        size_t total_chunk_size = sizeof(chunk_header_t) + chunk_payload_size;
        
        // Allocate chunk buffer
        uint8_t *chunk_buffer = malloc(total_chunk_size);
        if (!chunk_buffer) {
            ESP_LOGE(TAG, "❌ Failed to allocate chunk %u buffer", i);
            chunk_manager_free_send_result(result);
            return ESP_ERR_NO_MEM;
        }
        
        // Fill chunk header
        chunk_header_t *header = (chunk_header_t*)chunk_buffer;
        header->flags = CHUNK_FLAG_CHUNKED;
        if (i == chunks_needed - 1) {
            header->flags |= CHUNK_FLAG_FINAL;
        } else {
            header->flags |= CHUNK_FLAG_MORE;
        }
        header->chunk_idx = i;
        header->total_chunks = chunks_needed;
        header->frame_id = frame_id;
        header->chunk_size = chunk_payload_size;
        
        // Copy payload data
        memcpy(chunk_buffer + sizeof(chunk_header_t), data + offset, chunk_payload_size);
        
        result->chunks[i] = chunk_buffer;
        result->chunk_sizes[i] = total_chunk_size;
        offset += chunk_payload_size;
    }
    
    result->chunk_count = chunks_needed;
    result->frame_id = frame_id;
    
    stats_frames_sent++;
    
    ESP_LOGD(TAG, "📦 Frame %u chunked into %u chunks, total_size: %zu", 
             frame_id, chunks_needed, data_size);
             
    return ESP_OK;
}

esp_err_t chunk_manager_process(const uint8_t *chunk_data, size_t chunk_size, reassembly_result_t *result)
{
    if (!initialized || !chunk_data || !result || chunk_size < sizeof(chunk_header_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(result, 0, sizeof(reassembly_result_t));
    
    // Parse chunk header
    const chunk_header_t *header = (const chunk_header_t*)chunk_data;
    const uint8_t *payload = chunk_data + sizeof(chunk_header_t);
    size_t payload_size = header->chunk_size;
    
    // Validate header
    if (payload_size != chunk_size - sizeof(chunk_header_t)) {
        ESP_LOGE(TAG, "❌ Chunk size mismatch: header=%u, actual=%zu", 
                 header->chunk_size, chunk_size - sizeof(chunk_header_t));
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (header->chunk_idx >= header->total_chunks) {
        ESP_LOGE(TAG, "❌ Invalid chunk index: %u/%u", header->chunk_idx, header->total_chunks);
        return ESP_ERR_INVALID_ARG;
    }
    
    result->frame_id = header->frame_id;
    
    xSemaphoreTake(chunk_mutex, portMAX_DELAY);
    
    // Find or create context
    reassembly_context_t *ctx = find_context_by_id(header->frame_id);
    if (!ctx) {
        // New frame, create context
        ctx = find_free_context();
        if (!ctx) {
            ESP_LOGW(TAG, "⚠️ No free reassembly context for frame %u", header->frame_id);
            xSemaphoreGive(chunk_mutex);
            return ESP_ERR_NO_MEM;
        }
        
        // Initialize new context
        ctx->frame_id = header->frame_id;
        ctx->timestamp_ms = get_timestamp_ms();
        ctx->chunks_received = 0;
        ctx->total_chunks = header->total_chunks;
        ctx->current_size = 0;
        ctx->active = true;
        
        // Estimate total size (rough calculation)
        ctx->expected_size = header->total_chunks * (config.max_chunk_size - sizeof(chunk_header_t));
        ctx->buffer = malloc(ctx->expected_size);
        if (!ctx->buffer) {
            ESP_LOGE(TAG, "❌ Failed to allocate reassembly buffer for frame %u", header->frame_id);
            ctx->active = false;
            xSemaphoreGive(chunk_mutex);
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGD(TAG, "🆕 Created reassembly context for frame %u, %u chunks expected", 
                 header->frame_id, header->total_chunks);
    }
    
    // Check for duplicate chunk
    uint8_t chunk_bit = (1 << header->chunk_idx);
    if (ctx->chunks_received & chunk_bit) {
        ESP_LOGD(TAG, "🔄 Duplicate chunk %u for frame %u", header->chunk_idx, header->frame_id);
        result->is_duplicate = true;
        xSemaphoreGive(chunk_mutex);
        return ESP_OK;
    }
    
    // Add chunk to context
    size_t chunk_offset = header->chunk_idx * (config.max_chunk_size - sizeof(chunk_header_t));
    if (chunk_offset + payload_size <= ctx->expected_size) {
        memcpy(ctx->buffer + chunk_offset, payload, payload_size);
        ctx->chunks_received |= chunk_bit;
        ctx->current_size += payload_size;
        
        ESP_LOGD(TAG, "📝 Added chunk %u/%u for frame %u, size: %zu", 
                 header->chunk_idx + 1, header->total_chunks, header->frame_id, payload_size);
    }
    
    // Check if frame is complete
    uint8_t expected_mask = (1 << ctx->total_chunks) - 1;
    if (ctx->chunks_received == expected_mask) {
        // Frame complete!
        result->complete_frame = malloc(ctx->current_size);
        if (result->complete_frame) {
            // Calculate actual size for final reassembly
            size_t actual_size = 0;
            for (uint8_t i = 0; i < ctx->total_chunks; i++) {
                size_t chunk_start = i * (config.max_chunk_size - sizeof(chunk_header_t));
                size_t chunk_payload_size;
                
                if (i == ctx->total_chunks - 1) {
                    // Last chunk might be smaller
                    chunk_payload_size = ctx->current_size - actual_size;
                } else {
                    chunk_payload_size = config.max_chunk_size - sizeof(chunk_header_t);
                }
                
                memcpy(result->complete_frame + actual_size, ctx->buffer + chunk_start, chunk_payload_size);
                actual_size += chunk_payload_size;
            }
            
            result->frame_size = actual_size;
            result->is_complete = true;
            stats_frames_received++;
            
            ESP_LOGI(TAG, "✅ Frame %u completed, size: %zu bytes", header->frame_id, actual_size);
        }
        
        // Free context
        free_context(ctx);
    }
    
    xSemaphoreGive(chunk_mutex);
    return ESP_OK;
}

void chunk_manager_free_send_result(chunk_result_t *result)
{
    if (!result) return;
    
    if (result->chunks) {
        for (uint8_t i = 0; i < result->chunk_count; i++) {
            if (result->chunks[i]) {
                free(result->chunks[i]);
            }
        }
        free(result->chunks);
    }
    
    if (result->chunk_sizes) {
        free(result->chunk_sizes);
    }
    
    memset(result, 0, sizeof(chunk_result_t));
}

void chunk_manager_cleanup_expired(void)
{
    if (!initialized || !chunk_mutex) return;
    
    uint32_t current_time = get_timestamp_ms();
    
    xSemaphoreTake(chunk_mutex, portMAX_DELAY);
    
    for (int i = 0; i < config.max_concurrent_frames; i++) {
        reassembly_context_t *ctx = &contexts[i];
        if (ctx->active) {
            uint32_t age = current_time - ctx->timestamp_ms;
            if (age > config.reassembly_timeout_ms) {
                ESP_LOGW(TAG, "⏰ Frame %u timed out after %lums", ctx->frame_id, age);
                free_context(ctx);
                stats_timeouts++;
            }
        }
    }
    
    xSemaphoreGive(chunk_mutex);
}

void chunk_manager_get_stats(uint8_t *active_contexts, 
                           uint32_t *total_frames_sent,
                           uint32_t *total_frames_received, 
                           uint32_t *timeout_count)
{
    if (!initialized) return;
    
    if (active_contexts) {
        uint8_t count = 0;
        if (chunk_mutex) {
            xSemaphoreTake(chunk_mutex, portMAX_DELAY);
            for (int i = 0; i < config.max_concurrent_frames; i++) {
                if (contexts[i].active) count++;
            }
            xSemaphoreGive(chunk_mutex);
        }
        *active_contexts = count;
    }
    
    if (total_frames_sent) *total_frames_sent = stats_frames_sent;
    if (total_frames_received) *total_frames_received = stats_frames_received;
    if (timeout_count) *timeout_count = stats_timeouts;
}

void chunk_manager_deinit(void)
{
    if (!initialized) return;
    
    if (chunk_mutex) {
        xSemaphoreTake(chunk_mutex, portMAX_DELAY);
        
        // Free all active contexts
        if (contexts) {
            for (int i = 0; i < config.max_concurrent_frames; i++) {
                free_context(&contexts[i]);
            }
            free(contexts);
            contexts = NULL;
        }
        
        xSemaphoreGive(chunk_mutex);
        vSemaphoreDelete(chunk_mutex);
        chunk_mutex = NULL;
    }
    
    initialized = false;
    
    ESP_LOGI(TAG, "🧹 Chunk manager deinitialized");
}