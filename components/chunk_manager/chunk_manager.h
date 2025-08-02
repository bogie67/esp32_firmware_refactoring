#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generic chunking and reassembly service for transport layers */

/**
 * @brief Configuration for chunking behavior
 */
typedef struct {
    uint16_t max_chunk_size;        ///< Maximum chunk payload size (transport-specific)
    uint8_t header_size;            ///< Size of transport chunk header
    uint8_t max_concurrent_frames;  ///< Maximum concurrent reassembly contexts
    uint32_t reassembly_timeout_ms; ///< Timeout for incomplete frames
} chunk_config_t;

/**
 * @brief Generic chunk header (transport-agnostic)
 */
typedef struct {
    uint8_t flags;          ///< CHUNKED | FINAL | MORE flags
    uint8_t chunk_idx;      ///< Chunk index (0-based)
    uint8_t total_chunks;   ///< Total chunks in frame
    uint16_t frame_id;      ///< Unique frame identifier
    uint16_t chunk_size;    ///< Payload size in this chunk
} __attribute__((packed)) chunk_header_t;

/**
 * @brief Chunking result for send operation
 */
typedef struct {
    uint8_t **chunks;       ///< Array of chunk data (including headers)
    size_t *chunk_sizes;    ///< Array of chunk sizes
    uint8_t chunk_count;    ///< Number of chunks generated
    uint16_t frame_id;      ///< Assigned frame ID
} chunk_result_t;

/**
 * @brief Reassembly result for process operation
 */
typedef struct {
    uint8_t *complete_frame;  ///< Reconstructed frame (NULL if incomplete)
    size_t frame_size;        ///< Size of complete frame
    uint16_t frame_id;        ///< Frame ID that was completed
    bool is_complete;         ///< True if frame is fully reassembled
    bool is_duplicate;        ///< True if chunk was already received
} reassembly_result_t;

/**
 * @brief Chunk manager flags
 */
#define CHUNK_FLAG_CHUNKED   0x01  ///< Frame is chunked
#define CHUNK_FLAG_FINAL     0x02  ///< Last chunk in frame
#define CHUNK_FLAG_MORE      0x04  ///< More chunks follow

/**
 * @brief Initialize chunk manager with configuration
 * 
 * @param config Chunking configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t chunk_manager_init(const chunk_config_t *config);

/**
 * @brief Chunk a frame for transmission
 * 
 * @param data Frame data to chunk
 * @param data_size Size of frame data
 * @param result Output chunking result (caller must free)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t chunk_manager_send(const uint8_t *data, size_t data_size, chunk_result_t *result);

/**
 * @brief Process a received chunk for reassembly
 * 
 * @param chunk_data Received chunk data (including header)
 * @param chunk_size Size of received chunk
 * @param result Output reassembly result (caller must free complete_frame if not NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t chunk_manager_process(const uint8_t *chunk_data, size_t chunk_size, reassembly_result_t *result);

/**
 * @brief Free resources allocated by chunk_manager_send()
 * 
 * @param result Chunking result to free
 */
void chunk_manager_free_send_result(chunk_result_t *result);

/**
 * @brief Clean up expired reassembly contexts
 * 
 * Should be called periodically to prevent memory leaks
 */
void chunk_manager_cleanup_expired(void);

/**
 * @brief Get current chunk manager statistics
 * 
 * @param active_contexts Number of active reassembly contexts (output)
 * @param total_frames_sent Total frames chunked for sending (output) 
 * @param total_frames_received Total frames completely reassembled (output)
 * @param timeout_count Number of frames that timed out (output)
 */
void chunk_manager_get_stats(uint8_t *active_contexts, 
                           uint32_t *total_frames_sent,
                           uint32_t *total_frames_received, 
                           uint32_t *timeout_count);

/**
 * @brief Reset chunk manager and free all resources
 */
void chunk_manager_deinit(void);

#ifdef __cplusplus
}
#endif