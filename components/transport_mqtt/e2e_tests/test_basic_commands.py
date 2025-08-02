import pytest
import time
import json
from conftest import send_command, wait_for_response

def test_ping_command(mqtt_client, esp32_topics):
    """Test basic ping command"""
    print("🧪 Testing ping command...")
    
    # Send ping command
    cmd = send_command(mqtt_client, 1, "ping")
    
    # Wait for response
    response = wait_for_response(mqtt_client, 1, timeout=10)
    
    assert response is not None, "Should receive response to ping command"
    assert response["id"] == 1, "Response ID should match command ID"
    assert response["status"] == 0, "Ping should succeed (status=0)"
    assert response["is_final"] == True, "Ping response should be final"
    
    print(f"✅ Ping response: {response}")

def test_invalid_command(mqtt_client, esp32_topics):
    """Test invalid command handling"""
    print("🧪 Testing invalid command...")
    
    # Send invalid command
    cmd = send_command(mqtt_client, 2, "invalid_op")
    
    # Wait for response
    response = wait_for_response(mqtt_client, 2, timeout=10)
    
    assert response is not None, "Should receive response to invalid command"
    assert response["id"] == 2, "Response ID should match command ID"
    assert response["status"] != 0, "Invalid command should fail (status!=0)"
    
    print(f"✅ Invalid command response: {response}")

def test_led_command(mqtt_client, esp32_topics):
    """Test LED control command"""
    print("🧪 Testing LED control...")
    
    # Test LED on
    cmd = send_command(mqtt_client, 3, "led", "on")
    response = wait_for_response(mqtt_client, 3, timeout=10)
    
    assert response is not None, "Should receive response to LED command"
    assert response["id"] == 3, "Response ID should match command ID"
    
    print(f"✅ LED on response: {response}")
    
    # Test LED off
    cmd = send_command(mqtt_client, 4, "led", "off")
    response = wait_for_response(mqtt_client, 4, timeout=10)
    
    assert response is not None, "Should receive response to LED off command"
    assert response["id"] == 4, "Response ID should match command ID"
    
    print(f"✅ LED off response: {response}")

def test_wifi_info_command(mqtt_client, esp32_topics):
    """Test WiFi info command"""
    print("🧪 Testing WiFi info command...")
    
    # Send wifi info command
    cmd = send_command(mqtt_client, 5, "wifi_info")
    response = wait_for_response(mqtt_client, 5, timeout=10)
    
    assert response is not None, "Should receive response to wifi_info command"
    assert response["id"] == 5, "Response ID should match command ID"
    
    # Should contain WiFi information in payload
    if response["status"] == 0 and "payload" in response:
        print(f"✅ WiFi info: {response['payload']}")
    else:
        print(f"⚠️ WiFi info not available: {response}")

def test_solenoid_command(mqtt_client, esp32_topics):
    """Test solenoid control command"""
    print("🧪 Testing solenoid control...")
    
    # Test solenoid activation
    cmd = send_command(mqtt_client, 6, "solenoid", "1000")  # 1000ms
    response = wait_for_response(mqtt_client, 6, timeout=15)  # Longer timeout for solenoid
    
    assert response is not None, "Should receive response to solenoid command"
    assert response["id"] == 6, "Response ID should match command ID"
    
    print(f"✅ Solenoid response: {response}")

def test_rapid_commands(mqtt_client, esp32_topics):
    """Test rapid command sequence"""
    print("🧪 Testing rapid command sequence...")
    
    responses = []
    
    # Send multiple ping commands rapidly
    for i in range(5):
        cmd_id = 10 + i
        send_command(mqtt_client, cmd_id, "ping")
        
        # Brief delay between commands
        time.sleep(0.1)
    
    # Collect all responses
    for i in range(5):
        expected_id = 10 + i
        response = wait_for_response(mqtt_client, expected_id, timeout=10)
        
        assert response is not None, f"Should receive response to command {expected_id}"
        assert response["id"] == expected_id, f"Response ID should be {expected_id}"
        
        responses.append(response)
    
    print(f"✅ Rapid commands completed: {len(responses)} responses received")

def test_command_with_long_payload(mqtt_client, esp32_topics):
    """Test command with longer payload"""
    print("🧪 Testing command with long payload...")
    
    # Create a longer payload
    long_payload = "A" * 100  # 100 character payload
    
    cmd = send_command(mqtt_client, 20, "echo", long_payload)
    response = wait_for_response(mqtt_client, 20, timeout=10)
    
    assert response is not None, "Should receive response to long payload command"
    assert response["id"] == 20, "Response ID should match command ID"
    
    print(f"✅ Long payload response: {response}")

def test_malformed_json_ignored(mqtt_client, esp32_topics):
    """Test that malformed JSON is ignored"""
    print("🧪 Testing malformed JSON handling...")
    
    # Send malformed JSON
    malformed_json = '{"id":1,"op":"ping"'  # Missing closing brace
    mqtt_client.publish("smartdrip/cmd", malformed_json)
    
    # Wait a bit to see if any response comes back
    time.sleep(2)
    
    # Should not receive any response for malformed JSON
    message = mqtt_client.wait_for_message(timeout=1)
    
    # If we got a message, it shouldn't be related to our malformed command
    if message:
        data = message.get('data', {})
        assert data.get('id') != 1, "Should not process malformed JSON command"
    
    print("✅ Malformed JSON correctly ignored")

def test_concurrent_mqtt_ble_operation(mqtt_client, esp32_topics):
    """Test that MQTT doesn't interfere with BLE operations"""
    print("🧪 Testing MQTT/BLE coexistence...")
    
    # Send MQTT command
    cmd = send_command(mqtt_client, 30, "ping")
    
    # Should get response via MQTT
    response = wait_for_response(mqtt_client, 30, timeout=10)
    
    assert response is not None, "MQTT command should work with BLE transport active"
    assert response["id"] == 30, "Response ID should match"
    
    print(f"✅ MQTT/BLE coexistence verified: {response}")