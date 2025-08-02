#pragma once

#include "mqtt_client.h"
#include <stdbool.h>

/**
 * Mock MQTT client functions for testing transport_mqtt
 * 
 * These functions replace the real esp_mqtt_client_* functions
 * during testing to provide controlled, deterministic behavior.
 */

// Reset mock state (call before each test)
void mock_mqtt_reset(void);

// Simulate MQTT events
void mock_mqtt_simulate_connected(void);
void mock_mqtt_simulate_disconnected(void);
void mock_mqtt_simulate_data(const char *topic, const char *data, int data_len);

// Get mock state for verification
bool mock_mqtt_is_started(void);
bool mock_mqtt_is_connected(void);
const char* mock_mqtt_get_last_published_topic(void);
const char* mock_mqtt_get_last_published_data(void);
int mock_mqtt_get_last_published_len(void);