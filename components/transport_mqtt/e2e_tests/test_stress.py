import pytest
import time
import threading
import random
from conftest import send_command, wait_for_response

def test_high_frequency_commands(mqtt_client, esp32_topics):
    """Test high frequency command sending"""
    print("🧪 Testing high frequency commands...")
    
    num_commands = 20
    responses = []
    
    # Send commands as fast as possible
    start_time = time.time()
    
    for i in range(num_commands):
        cmd_id = 100 + i
        send_command(mqtt_client, cmd_id, "ping")
    
    send_duration = time.time() - start_time
    print(f"📤 Sent {num_commands} commands in {send_duration:.2f}s")
    
    # Collect responses
    start_time = time.time()
    
    for i in range(num_commands):
        expected_id = 100 + i
        response = wait_for_response(mqtt_client, expected_id, timeout=15)
        
        if response:
            responses.append(response)
        else:
            print(f"⚠️ No response for command {expected_id}")
    
    receive_duration = time.time() - start_time
    print(f"📨 Received {len(responses)} responses in {receive_duration:.2f}s")
    
    # Verify we got most responses (allow some loss under stress)
    success_rate = len(responses) / num_commands
    assert success_rate >= 0.8, f"Success rate too low: {success_rate:.2f}"
    
    print(f"✅ High frequency test: {success_rate:.2%} success rate")

def test_concurrent_clients(mqtt_client, esp32_topics):
    """Test multiple concurrent MQTT clients"""
    print("🧪 Testing concurrent MQTT clients...")
    
    # This test simulates multiple devices/apps connecting
    responses = []
    errors = []
    
    def client_worker(client_id, start_cmd_id):
        try:
            for i in range(5):
                cmd_id = start_cmd_id + i
                send_command(mqtt_client, cmd_id, "ping")
                
                response = wait_for_response(mqtt_client, cmd_id, timeout=10)
                if response:
                    responses.append(response)
                else:
                    errors.append(f"No response for client {client_id} cmd {cmd_id}")
                
                time.sleep(0.1)  # Small delay between commands
        except Exception as e:
            errors.append(f"Client {client_id} error: {e}")
    
    # Start multiple "clients" (threads using same MQTT connection)
    threads = []
    for client_id in range(3):
        start_cmd_id = 200 + (client_id * 10)
        thread = threading.Thread(target=client_worker, args=(client_id, start_cmd_id))
        threads.append(thread)
        thread.start()
    
    # Wait for all threads to complete
    for thread in threads:
        thread.join()
    
    print(f"📊 Concurrent test results:")
    print(f"  - Responses received: {len(responses)}")
    print(f"  - Errors: {len(errors)}")
    
    for error in errors[:5]:  # Show first 5 errors
        print(f"    ❌ {error}")
    
    # Should handle concurrent access reasonably well
    success_rate = len(responses) / 15  # 3 clients * 5 commands each
    assert success_rate >= 0.7, f"Concurrent access success rate too low: {success_rate:.2f}"
    
    print(f"✅ Concurrent clients test: {success_rate:.2%} success rate")

def test_message_burst(mqtt_client, esp32_topics):
    """Test burst of messages followed by quiet period"""
    print("🧪 Testing message burst pattern...")
    
    responses = []
    
    # Send burst of 10 commands
    print("📤 Sending burst of 10 commands...")
    for i in range(10):
        cmd_id = 300 + i
        send_command(mqtt_client, cmd_id, "ping")
    
    # Wait for all responses
    for i in range(10):
        expected_id = 300 + i
        response = wait_for_response(mqtt_client, expected_id, timeout=15)
        if response:
            responses.append(response)
    
    burst_responses = len(responses)
    print(f"📨 Burst responses: {burst_responses}/10")
    
    # Quiet period
    print("🤫 Quiet period (3 seconds)...")
    time.sleep(3)
    
    # Send single command after quiet period
    cmd = send_command(mqtt_client, 400, "ping")
    response = wait_for_response(mqtt_client, 400, timeout=10)
    
    assert response is not None, "Should respond after quiet period"
    assert response["id"] == 400, "Response ID should match"
    
    print(f"✅ Message burst test: {burst_responses}/10 burst, 1/1 after quiet")

def test_payload_sizes(mqtt_client, esp32_topics):
    """Test various payload sizes"""
    print("🧪 Testing various payload sizes...")
    
    payload_sizes = [0, 10, 50, 100, 200]  # Different payload sizes
    responses = []
    
    for i, size in enumerate(payload_sizes):
        cmd_id = 500 + i
        
        if size == 0:
            payload = None
        else:
            payload = "X" * size
        
        print(f"📤 Testing payload size: {size} bytes")
        send_command(mqtt_client, cmd_id, "echo", payload)
        
        response = wait_for_response(mqtt_client, cmd_id, timeout=10)
        
        if response:
            responses.append(response)
            print(f"  ✅ Response received for {size} byte payload")
        else:
            print(f"  ❌ No response for {size} byte payload")
    
    success_rate = len(responses) / len(payload_sizes)
    assert success_rate >= 0.8, f"Payload size test success rate too low: {success_rate:.2f}"
    
    print(f"✅ Payload size test: {len(responses)}/{len(payload_sizes)} succeeded")

def test_connection_resilience(mqtt_client, esp32_topics):
    """Test system behavior under connection stress"""
    print("🧪 Testing connection resilience...")
    
    # Send commands while simulating connection issues
    responses = []
    
    for i in range(5):
        cmd_id = 600 + i
        
        # Send command
        send_command(mqtt_client, cmd_id, "ping")
        
        # Simulate brief network issue by disconnecting/reconnecting
        if i == 2:  # Disconnect in the middle
            print("🔌 Simulating brief disconnection...")
            mqtt_client.disconnect()
            time.sleep(1)
            mqtt_client.connect()
            mqtt_client.subscribe("smartdrip/resp")
            print("🔗 Reconnected")
        
        # Wait for response
        response = wait_for_response(mqtt_client, cmd_id, timeout=15)
        if response:
            responses.append(response)
    
    print(f"📊 Resilience test: {len(responses)}/5 responses received")
    
    # Should recover and continue working
    final_test = send_command(mqtt_client, 700, "ping")
    final_response = wait_for_response(mqtt_client, 700, timeout=10)
    
    assert final_response is not None, "Should work after connection stress"
    print("✅ Connection resilience verified")

def test_random_command_sequence(mqtt_client, esp32_topics):
    """Test random sequence of different commands"""
    print("🧪 Testing random command sequence...")
    
    commands = ["ping", "led", "wifi_info"]
    payloads = [None, "on", "off", "test"]
    
    responses = []
    
    for i in range(15):
        cmd_id = 800 + i
        op = random.choice(commands)
        payload = random.choice(payloads) if op == "led" else None
        
        print(f"📤 Command {i+1}: {op} {payload if payload else ''}")
        send_command(mqtt_client, cmd_id, op, payload)
        
        response = wait_for_response(mqtt_client, cmd_id, timeout=10)
        if response:
            responses.append(response)
            print(f"  ✅ Response: status={response['status']}")
        else:
            print(f"  ❌ No response")
        
        # Random delay between commands
        time.sleep(random.uniform(0.1, 0.5))
    
    success_rate = len(responses) / 15
    assert success_rate >= 0.8, f"Random sequence success rate too low: {success_rate:.2f}"
    
    print(f"✅ Random sequence test: {success_rate:.2%} success rate")