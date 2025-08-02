/* error_manager.c
 * Unified Error Management System for ESP32 Firmware
 * Provides centralized error reporting, recovery, and monitoring
 */

#include "error_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "ERROR_MGR";

/* ──────────────── Internal State ──────────────── */

static bool error_manager_initialized = false;
static SemaphoreHandle_t error_manager_mutex = NULL;

// Global error callback
static unified_error_callback_t global_error_callback = NULL;
static void *global_callback_user_data = NULL;

// Component registration data
typedef struct {
    bool registered;
    component_recovery_config_t recovery_config;
    component_recovery_callback_t recovery_callback;
    void *user_data;
    uint32_t consecutive_errors;
    uint32_t last_recovery_timestamp_ms;
} component_registration_t;

static component_registration_t components[ERROR_COMPONENT_MAX];
static component_error_stats_t component_stats[ERROR_COMPONENT_MAX];
static system_error_stats_t system_stats = {0};

// Default recovery configuration
static const component_recovery_config_t default_recovery_config = {
    .max_consecutive_errors = 5,
    .recovery_cooldown_ms = 10000,  // 10 seconds
    .retry_delay_ms = 1000,         // 1 second
    .auto_recovery_enabled = true,
    .escalate_on_failure = true
};

/* ──────────────── Utility Functions ──────────────── */

const char* error_manager_get_component_name(error_component_t component)
{
    switch (component) {
        case ERROR_COMPONENT_SYSTEM: return "SYSTEM";
        case ERROR_COMPONENT_BLE_TRANSPORT: return "BLE_TRANSPORT";
        case ERROR_COMPONENT_MQTT_TRANSPORT: return "MQTT_TRANSPORT";
        case ERROR_COMPONENT_CMD_PROCESSOR: return "CMD_PROCESSOR";
        case ERROR_COMPONENT_CHUNK_MANAGER: return "CHUNK_MANAGER";
        case ERROR_COMPONENT_WIFI: return "WIFI";
        case ERROR_COMPONENT_SOLENOID: return "SOLENOID";
        case ERROR_COMPONENT_SCHEDULE: return "SCHEDULE";
        case ERROR_COMPONENT_CODEC: return "CODEC";
        default: return "UNKNOWN";
    }
}

const char* error_manager_get_category_name(error_category_t category)
{
    switch (category) {
        case ERROR_CATEGORY_NONE: return "NONE";
        case ERROR_CATEGORY_CONNECTION: return "CONNECTION";
        case ERROR_CATEGORY_COMMUNICATION: return "COMMUNICATION";
        case ERROR_CATEGORY_PROTOCOL: return "PROTOCOL";
        case ERROR_CATEGORY_RESOURCE: return "RESOURCE";
        case ERROR_CATEGORY_MEMORY: return "MEMORY";
        case ERROR_CATEGORY_QUEUE: return "QUEUE";
        case ERROR_CATEGORY_PROCESSING: return "PROCESSING";
        case ERROR_CATEGORY_VALIDATION: return "VALIDATION";
        case ERROR_CATEGORY_TIMEOUT: return "TIMEOUT";
        case ERROR_CATEGORY_HARDWARE: return "HARDWARE";
        case ERROR_CATEGORY_SYSTEM: return "SYSTEM";
        case ERROR_CATEGORY_CONFIGURATION: return "CONFIGURATION";
        case ERROR_CATEGORY_RECOVERY: return "RECOVERY";
        default: return "UNKNOWN";
    }
}

const char* error_manager_get_severity_name(error_severity_t severity)
{
    switch (severity) {
        case ERROR_SEVERITY_INFO: return "INFO";
        case ERROR_SEVERITY_WARNING: return "WARNING";
        case ERROR_SEVERITY_ERROR: return "ERROR";
        case ERROR_SEVERITY_CRITICAL: return "CRITICAL";
        case ERROR_SEVERITY_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

const char* error_manager_get_recovery_description(error_recovery_strategy_t strategy)
{
    switch (strategy) {
        case ERROR_RECOVERY_NONE: return "No automatic recovery";
        case ERROR_RECOVERY_RETRY: return "Retry operation";
        case ERROR_RECOVERY_RESET_STATE: return "Reset component state";
        case ERROR_RECOVERY_RESTART_COMPONENT: return "Restart component";
        case ERROR_RECOVERY_RESTART_SERVICE: return "Restart service";
        case ERROR_RECOVERY_SYSTEM_RESTART: return "System restart required";
        case ERROR_RECOVERY_CUSTOM: return "Custom recovery strategy";
        default: return "Unknown recovery strategy";
    }
}

/* ──────────────── Internal Functions ──────────────── */

/**
 * @brief Get current timestamp in milliseconds
 */
static uint32_t get_timestamp_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/**
 * @brief Update component statistics
 */
static void update_component_statistics(error_component_t component, error_category_t category, error_severity_t severity, uint32_t error_code)
{
    if (component >= ERROR_COMPONENT_MAX || category >= ERROR_CATEGORY_MAX || severity > ERROR_SEVERITY_FATAL) {
        return;
    }
    
    component_error_stats_t *stats = &component_stats[component];
    
    stats->total_errors++;
    stats->errors_by_category[category]++;
    stats->errors_by_severity[severity]++;
    stats->last_error_timestamp_ms = get_timestamp_ms();
    stats->last_error_code = error_code;
    stats->last_error_category = category;
}

/**
 * @brief Update system statistics
 */
static void update_system_statistics(error_component_t component, error_severity_t severity)
{
    system_stats.total_system_errors++;
    system_stats.errors_by_component[component]++;
    
    if (severity >= ERROR_SEVERITY_CRITICAL) {
        system_stats.last_critical_error_ms = get_timestamp_ms();
    }
    
    // Update most error-prone component
    uint32_t max_errors = 0;
    for (int i = 0; i < ERROR_COMPONENT_MAX; i++) {
        if (system_stats.errors_by_component[i] > max_errors) {
            max_errors = system_stats.errors_by_component[i];
            system_stats.most_error_prone_component = (error_component_t)i;
        }
    }
}

/**
 * @brief Determine recovery strategy based on category and severity
 */
static error_recovery_strategy_t get_default_recovery_strategy(error_category_t category, error_severity_t severity)
{
    // Critical and fatal errors require more aggressive recovery
    if (severity >= ERROR_SEVERITY_CRITICAL) {
        switch (category) {
            case ERROR_CATEGORY_CONNECTION:
            case ERROR_CATEGORY_COMMUNICATION:
                return ERROR_RECOVERY_RESTART_COMPONENT;
            case ERROR_CATEGORY_MEMORY:
            case ERROR_CATEGORY_RESOURCE:
                return ERROR_RECOVERY_RESET_STATE;
            case ERROR_CATEGORY_HARDWARE:
            case ERROR_CATEGORY_SYSTEM:
                return ERROR_RECOVERY_SYSTEM_RESTART;
            default:
                return ERROR_RECOVERY_RESTART_COMPONENT;
        }
    }
    
    // Non-critical errors use lighter recovery
    switch (category) {
        case ERROR_CATEGORY_CONNECTION:
        case ERROR_CATEGORY_COMMUNICATION:
        case ERROR_CATEGORY_TIMEOUT:
            return ERROR_RECOVERY_RETRY;
        case ERROR_CATEGORY_MEMORY:
        case ERROR_CATEGORY_RESOURCE:
        case ERROR_CATEGORY_QUEUE:
            return ERROR_RECOVERY_RETRY;
        case ERROR_CATEGORY_PROTOCOL:
        case ERROR_CATEGORY_VALIDATION:
            return ERROR_RECOVERY_RESET_STATE;
        case ERROR_CATEGORY_CONFIGURATION:
            return ERROR_RECOVERY_NONE;  // Requires manual intervention
        default:
            return ERROR_RECOVERY_RETRY;
    }
}

/**
 * @brief Check if component should attempt recovery
 */
static bool should_attempt_recovery(error_component_t component, error_severity_t severity)
{
    if (component >= ERROR_COMPONENT_MAX) {
        return false;
    }
    
    component_registration_t *reg = &components[component];
    
    if (!reg->registered || !reg->recovery_config.auto_recovery_enabled) {
        return false;
    }
    
    // Always try to recover from critical/fatal errors
    if (severity >= ERROR_SEVERITY_CRITICAL) {
        return true;
    }
    
    // Check consecutive error threshold
    if (reg->consecutive_errors >= reg->recovery_config.max_consecutive_errors) {
        ESP_LOGW(TAG, "🚫 Component %s: max consecutive errors reached (%" PRIu32 ")",
                 error_manager_get_component_name(component), reg->consecutive_errors);
        return false;
    }
    
    // Check recovery cooldown
    uint32_t current_time = get_timestamp_ms();
    if ((current_time - reg->last_recovery_timestamp_ms) < reg->recovery_config.recovery_cooldown_ms) {
        ESP_LOGD(TAG, "⏳ Component %s: recovery cooldown active",
                 error_manager_get_component_name(component));
        return false;
    }
    
    return true;
}

/**
 * @brief Execute recovery strategy
 */
static esp_err_t execute_recovery(error_component_t component, error_recovery_strategy_t strategy, const unified_error_info_t *error_info)
{
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    component_registration_t *reg = &components[component];
    
    ESP_LOGI(TAG, "🔧 Executing recovery for %s: %s",
             error_manager_get_component_name(component),
             error_manager_get_recovery_description(strategy));
    
    // Update recovery statistics
    component_stats[component].recovery_attempts++;
    system_stats.total_recovery_attempts++;
    reg->last_recovery_timestamp_ms = get_timestamp_ms();
    
    esp_err_t result = ESP_FAIL;
    
    // Try custom recovery callback first
    if (reg->recovery_callback && strategy == ERROR_RECOVERY_CUSTOM) {
        result = reg->recovery_callback(error_info, reg->user_data);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "✅ Custom recovery succeeded for %s", error_manager_get_component_name(component));
            component_stats[component].recovery_successes++;
            system_stats.total_recovery_successes++;
            reg->consecutive_errors = 0;  // Reset on success
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "❌ Custom recovery failed for %s", error_manager_get_component_name(component));
        }
    }
    
    // Execute built-in recovery strategies
    switch (strategy) {
        case ERROR_RECOVERY_RETRY:
            // For retry, just add delay and reset consecutive errors after delay
            vTaskDelay(pdMS_TO_TICKS(reg->recovery_config.retry_delay_ms));
            result = ESP_OK;
            break;
            
        case ERROR_RECOVERY_RESET_STATE:
            // Reset state is component-specific, try custom callback
            if (reg->recovery_callback) {
                result = reg->recovery_callback(error_info, reg->user_data);
            } else {
                ESP_LOGW(TAG, "⚠️ No state reset handler for %s", error_manager_get_component_name(component));
                result = ESP_ERR_NOT_SUPPORTED;
            }
            break;
            
        case ERROR_RECOVERY_RESTART_COMPONENT:
        case ERROR_RECOVERY_RESTART_SERVICE:
            // Component restart requires custom implementation
            if (reg->recovery_callback) {
                result = reg->recovery_callback(error_info, reg->user_data);
            } else {
                ESP_LOGW(TAG, "⚠️ No restart handler for %s", error_manager_get_component_name(component));
                result = ESP_ERR_NOT_SUPPORTED;
            }
            break;
            
        case ERROR_RECOVERY_SYSTEM_RESTART:
            ESP_LOGE(TAG, "🚨 System restart requested for %s - logging only (safety)", 
                     error_manager_get_component_name(component));
            result = ESP_ERR_NOT_SUPPORTED;  // Don't actually restart
            break;
            
        case ERROR_RECOVERY_NONE:
        default:
            ESP_LOGD(TAG, "ℹ️ No recovery action for %s", error_manager_get_component_name(component));
            result = ESP_ERR_NOT_SUPPORTED;
            break;
    }
    
    if (result == ESP_OK) {
        component_stats[component].recovery_successes++;
        system_stats.total_recovery_successes++;
        reg->consecutive_errors = 0;  // Reset on successful recovery
        ESP_LOGI(TAG, "✅ Recovery succeeded for %s", error_manager_get_component_name(component));
    } else {
        ESP_LOGW(TAG, "❌ Recovery failed for %s", error_manager_get_component_name(component));
        
        // Escalate if configured
        if (reg->recovery_config.escalate_on_failure && strategy != ERROR_RECOVERY_SYSTEM_RESTART) {
            ESP_LOGW(TAG, "⬆️ Escalating recovery for %s", error_manager_get_component_name(component));
            // Try next level recovery strategy
            error_recovery_strategy_t escalated_strategy = (error_recovery_strategy_t)(strategy + 1);
            if (escalated_strategy < ERROR_RECOVERY_SYSTEM_RESTART) {
                return execute_recovery(component, escalated_strategy, error_info);
            }
        }
    }
    
    return result;
}

/* ──────────────── Core API Implementation ──────────────── */

esp_err_t error_manager_init(void)
{
    if (error_manager_initialized) {
        ESP_LOGW(TAG, "⚠️ Error manager already initialized");
        return ESP_OK;
    }
    
    // Create mutex for thread safety
    error_manager_mutex = xSemaphoreCreateMutex();
    if (!error_manager_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create error manager mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize all data structures
    memset(components, 0, sizeof(components));
    memset(component_stats, 0, sizeof(component_stats));
    memset(&system_stats, 0, sizeof(system_stats));
    
    global_error_callback = NULL;
    global_callback_user_data = NULL;
    
    error_manager_initialized = true;
    
    ESP_LOGI(TAG, "✅ Unified error management system initialized");
    return ESP_OK;
}

esp_err_t error_manager_deinit(void)
{
    if (!error_manager_initialized) {
        return ESP_OK;
    }
    
    if (error_manager_mutex) {
        vSemaphoreDelete(error_manager_mutex);
        error_manager_mutex = NULL;
    }
    
    // Clear all data
    memset(components, 0, sizeof(components));
    memset(component_stats, 0, sizeof(component_stats));
    memset(&system_stats, 0, sizeof(system_stats));
    
    global_error_callback = NULL;
    global_callback_user_data = NULL;
    
    error_manager_initialized = false;
    
    ESP_LOGI(TAG, "✅ Error management system deinitialized");
    return ESP_OK;
}

esp_err_t error_manager_register_component(error_component_t component,
                                          const component_recovery_config_t *recovery_config,
                                          component_recovery_callback_t recovery_callback,
                                          void *user_data)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    component_registration_t *reg = &components[component];
    
    reg->registered = true;
    reg->recovery_callback = recovery_callback;
    reg->user_data = user_data;
    reg->consecutive_errors = 0;
    reg->last_recovery_timestamp_ms = 0;
    
    // Use provided config or default
    if (recovery_config) {
        reg->recovery_config = *recovery_config;
    } else {
        reg->recovery_config = default_recovery_config;
    }
    
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Component registered: %s (auto_recovery=%s)",
             error_manager_get_component_name(component),
             reg->recovery_config.auto_recovery_enabled ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t error_manager_report(error_component_t component,
                              error_category_t category,
                              error_severity_t severity,
                              uint32_t error_code,
                              esp_err_t esp_code,
                              uint32_t context_data,
                              const char *description)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (component >= ERROR_COMPONENT_MAX || category >= ERROR_CATEGORY_MAX || severity > ERROR_SEVERITY_FATAL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    // Create unified error info
    unified_error_info_t error_info = {
        .component = component,
        .category = category,
        .severity = severity,
        .recovery = get_default_recovery_strategy(category, severity),
        .error_code = error_code,
        .esp_error_code = esp_code,
        .timestamp_ms = get_timestamp_ms(),
        .context_data = context_data
    };
    
    // Copy description safely
    if (description) {
        strncpy(error_info.description, description, sizeof(error_info.description) - 1);
        error_info.description[sizeof(error_info.description) - 1] = '\0';
    } else {
        snprintf(error_info.description, sizeof(error_info.description),
                "%s error in %s", 
                error_manager_get_category_name(category),
                error_manager_get_component_name(component));
    }
    
    // Copy component info
    strncpy(error_info.component_info, error_manager_get_component_name(component),
            sizeof(error_info.component_info) - 1);
    error_info.component_info[sizeof(error_info.component_info) - 1] = '\0';
    
    // Update statistics
    update_component_statistics(component, category, severity, error_code);
    update_system_statistics(component, severity);
    
    // Update consecutive error count
    if (component < ERROR_COMPONENT_MAX) {
        components[component].consecutive_errors++;
    }
    
    xSemaphoreGive(error_manager_mutex);
    
    // Log error based on severity
    const char *severity_emoji[] = {"ℹ️", "⚠️", "❌", "🚨", "💀"};
    const char *emoji = (severity <= ERROR_SEVERITY_FATAL) ? severity_emoji[severity] : "❓";
    
    switch (severity) {
        case ERROR_SEVERITY_INFO:
            ESP_LOGI(TAG, "%s [%s/%s] %s (code=%" PRIu32 ", esp_err=%d, ctx=%" PRIu32 ")",
                     emoji, error_info.component_info, error_manager_get_category_name(category),
                     error_info.description, error_code, esp_code, context_data);
            break;
        case ERROR_SEVERITY_WARNING:
            ESP_LOGW(TAG, "%s [%s/%s] %s (code=%" PRIu32 ", esp_err=%d, ctx=%" PRIu32 ")",
                     emoji, error_info.component_info, error_manager_get_category_name(category),
                     error_info.description, error_code, esp_code, context_data);
            break;
        case ERROR_SEVERITY_ERROR:
        case ERROR_SEVERITY_CRITICAL:
        case ERROR_SEVERITY_FATAL:
            ESP_LOGE(TAG, "%s [%s/%s] %s (code=%" PRIu32 ", esp_err=%d, ctx=%" PRIu32 ")",
                     emoji, error_info.component_info, error_manager_get_category_name(category),
                     error_info.description, error_code, esp_code, context_data);
            break;
    }
    
    // Call global callback if registered
    if (global_error_callback) {
        global_error_callback(&error_info, global_callback_user_data);
    }
    
    // Attempt automatic recovery if appropriate
    if (should_attempt_recovery(component, severity)) {
        esp_err_t recovery_result = execute_recovery(component, error_info.recovery, &error_info);
        if (recovery_result != ESP_OK) {
            ESP_LOGW(TAG, "🔧 Automatic recovery failed for %s", error_manager_get_component_name(component));
        }
    }
    
    return ESP_OK;
}

esp_err_t error_manager_register_global_callback(unified_error_callback_t callback, void *user_data)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    global_error_callback = callback;
    global_callback_user_data = user_data;
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Global error callback registered");
    return ESP_OK;
}

esp_err_t error_manager_unregister_global_callback(void)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    global_error_callback = NULL;
    global_callback_user_data = NULL;
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Global error callback unregistered");
    return ESP_OK;
}

/* ──────────────── Statistics API Implementation ──────────────── */

esp_err_t error_manager_get_component_stats(error_component_t component, component_error_stats_t *stats)
{
    if (!error_manager_initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    *stats = component_stats[component];
    xSemaphoreGive(error_manager_mutex);
    
    return ESP_OK;
}

esp_err_t error_manager_get_system_stats(system_error_stats_t *stats)
{
    if (!error_manager_initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    *stats = system_stats;
    stats->system_uptime_ms = get_timestamp_ms();
    xSemaphoreGive(error_manager_mutex);
    
    return ESP_OK;
}

esp_err_t error_manager_reset_component_stats(error_component_t component)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    memset(&component_stats[component], 0, sizeof(component_error_stats_t));
    if (component < ERROR_COMPONENT_MAX) {
        components[component].consecutive_errors = 0;
    }
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Statistics reset for component: %s", error_manager_get_component_name(component));
    return ESP_OK;
}

esp_err_t error_manager_reset_system_stats(void)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    memset(&system_stats, 0, sizeof(system_error_stats_t));
    memset(component_stats, 0, sizeof(component_stats));
    
    // Reset consecutive errors for all components
    for (int i = 0; i < ERROR_COMPONENT_MAX; i++) {
        components[i].consecutive_errors = 0;
    }
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ All error statistics reset");
    return ESP_OK;
}

/* ──────────────── Recovery API Implementation ──────────────── */

esp_err_t error_manager_force_recovery(error_component_t component,
                                      error_recovery_strategy_t strategy,
                                      bool force_recovery)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!force_recovery && !components[component].registered) {
        ESP_LOGW(TAG, "⚠️ Component %s not registered for recovery", error_manager_get_component_name(component));
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create fake error info for recovery
    unified_error_info_t error_info = {
        .component = component,
        .category = ERROR_CATEGORY_RECOVERY,
        .severity = ERROR_SEVERITY_WARNING,
        .recovery = strategy,
        .error_code = 0,
        .esp_error_code = ESP_OK,
        .timestamp_ms = get_timestamp_ms(),
        .context_data = 0
    };
    
    snprintf(error_info.description, sizeof(error_info.description),
             "Manual recovery requested for %s", error_manager_get_component_name(component));
    strncpy(error_info.component_info, error_manager_get_component_name(component),
            sizeof(error_info.component_info) - 1);
    
    ESP_LOGI(TAG, "🔧 Manual recovery requested for %s: %s",
             error_manager_get_component_name(component),
             error_manager_get_recovery_description(strategy));
    
    return execute_recovery(component, strategy, &error_info);
}

esp_err_t error_manager_configure_component_recovery(error_component_t component,
                                                    const component_recovery_config_t *config)
{
    if (!error_manager_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate configuration
    if (config->max_consecutive_errors == 0 || config->max_consecutive_errors > 100) {
        ESP_LOGE(TAG, "❌ Invalid max_consecutive_errors: %" PRIu32, config->max_consecutive_errors);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->recovery_cooldown_ms > 300000) {  // Max 5 minutes
        ESP_LOGE(TAG, "❌ Invalid recovery_cooldown_ms: %" PRIu32, config->recovery_cooldown_ms);
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    if (!components[component].registered) {
        // Register component with default settings if not already registered
        components[component].registered = true;
        components[component].recovery_callback = NULL;
        components[component].user_data = NULL;
        components[component].consecutive_errors = 0;
        components[component].last_recovery_timestamp_ms = 0;
    }
    
    components[component].recovery_config = *config;
    
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Recovery configuration updated for %s: max_errors=%" PRIu32 ", cooldown=%" PRIu32 "ms, auto=%s",
             error_manager_get_component_name(component),
             config->max_consecutive_errors, config->recovery_cooldown_ms,
             config->auto_recovery_enabled ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t error_manager_set_auto_recovery(error_component_t component, bool enabled)
{
    if (!error_manager_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (component >= ERROR_COMPONENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    if (!components[component].registered) {
        // Register component with default settings if not already registered
        components[component].registered = true;
        components[component].recovery_config = default_recovery_config;
        components[component].recovery_callback = NULL;
        components[component].user_data = NULL;
        components[component].consecutive_errors = 0;
        components[component].last_recovery_timestamp_ms = 0;
    }
    
    components[component].recovery_config.auto_recovery_enabled = enabled;
    
    xSemaphoreGive(error_manager_mutex);
    
    ESP_LOGI(TAG, "✅ Auto recovery %s for component: %s",
             enabled ? "enabled" : "disabled",
             error_manager_get_component_name(component));
    
    return ESP_OK;
}

/* ──────────────── System Health API Implementation ──────────────── */

error_severity_t error_manager_get_system_health(void)
{
    if (!error_manager_initialized) {
        return ERROR_SEVERITY_ERROR;  // System not properly initialized
    }
    
    error_severity_t max_severity = ERROR_SEVERITY_INFO;
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    uint32_t current_time = get_timestamp_ms();
    
    for (int i = 0; i < ERROR_COMPONENT_MAX; i++) {
        component_error_stats_t *stats = &component_stats[i];
        
        // Check for recent critical errors (within last 5 minutes)
        if (stats->last_error_timestamp_ms > 0 && 
            (current_time - stats->last_error_timestamp_ms) < 300000) {
            
            // Find highest severity from recent errors
            for (int sev = ERROR_SEVERITY_FATAL; sev >= ERROR_SEVERITY_INFO; sev--) {
                if (stats->errors_by_severity[sev] > 0) {
                    if (sev > max_severity) {
                        max_severity = (error_severity_t)sev;
                    }
                    break;
                }
            }
        }
        
        // Check for high consecutive error count
        if (components[i].consecutive_errors >= components[i].recovery_config.max_consecutive_errors) {
            if (ERROR_SEVERITY_WARNING > max_severity) {
                max_severity = ERROR_SEVERITY_WARNING;
            }
        }
    }
    
    xSemaphoreGive(error_manager_mutex);
    
    return max_severity;
}

bool error_manager_is_component_degraded(error_component_t component)
{
    if (!error_manager_initialized || component >= ERROR_COMPONENT_MAX) {
        return true;  // Consider unknown/uninitialized as degraded
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    component_error_stats_t *stats = &component_stats[component];
    uint32_t current_time = get_timestamp_ms();
    bool degraded = false;
    
    // Component is degraded if:
    // 1. Has recent critical errors (within last 2 minutes)
    // 2. Has high consecutive error count
    // 3. Has many recent errors
    
    if (stats->last_error_timestamp_ms > 0 && 
        (current_time - stats->last_error_timestamp_ms) < 120000) {  // 2 minutes
        
        // Check for critical/fatal errors
        if (stats->errors_by_severity[ERROR_SEVERITY_CRITICAL] > 0 ||
            stats->errors_by_severity[ERROR_SEVERITY_FATAL] > 0) {
            degraded = true;
        }
    }
    
    // Check consecutive errors
    if (components[component].consecutive_errors >= (components[component].recovery_config.max_consecutive_errors / 2)) {
        degraded = true;
    }
    
    xSemaphoreGive(error_manager_mutex);
    
    return degraded;
}

uint32_t error_manager_time_since_last_critical_error(void)
{
    if (!error_manager_initialized) {
        return 0;
    }
    
    xSemaphoreTake(error_manager_mutex, portMAX_DELAY);
    
    uint32_t time_since = 0;
    if (system_stats.last_critical_error_ms > 0) {
        uint32_t current_time = get_timestamp_ms();
        if (current_time >= system_stats.last_critical_error_ms) {
            time_since = current_time - system_stats.last_critical_error_ms;
        }
    }
    
    xSemaphoreGive(error_manager_mutex);
    
    return time_since;
}