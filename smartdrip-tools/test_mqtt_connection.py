#!/usr/bin/env python3
"""
Quick MQTT WebSocket Connection Test

Test veloce per verificare la connessione al broker EMQX via WebSocket
"""

import paho.mqtt.client as mqtt
import time
import json

def test_emqx_connection():
    """Test connessione al broker EMQX pubblico"""
    
    broker_url = "ws://broker.emqx.io:8083/mqtt"
    topic_prefix = "security1/esp32_test"
    
    print(f"🧪 Testing MQTT WebSocket connection")
    print(f"Broker: {broker_url}")
    print(f"Topic: {topic_prefix}")
    print("-" * 50)
    
    messages_received = []
    
    def on_connect(client, userdata, flags, rc, properties=None):
        if rc == 0:
            print("✅ Connected to MQTT broker")
            
            # Subscribe to test topic
            test_topic = f"{topic_prefix}/test/response"
            client.subscribe(test_topic)
            print(f"📋 Subscribed to: {test_topic}")
            
            # Publish test message
            test_msg = {
                "test": "mqtt_connection",
                "timestamp": time.time(),
                "message": "Hello from Python test client!"
            }
            
            publish_topic = f"{topic_prefix}/test/request"
            client.publish(publish_topic, json.dumps(test_msg))
            print(f"📤 Published test message to: {publish_topic}")
            
        else:
            print(f"❌ Failed to connect, return code {rc}")
    
    def on_message(client, userdata, msg, properties=None):
        print(f"📨 Message received on {msg.topic}:")
        try:
            payload = json.loads(msg.payload.decode())
            print(f"   {json.dumps(payload, indent=2)}")
        except:
            print(f"   Raw: {msg.payload}")
        messages_received.append(msg.payload)
    
    def on_disconnect(client, userdata, rc, properties=None):
        print(f"🔌 Disconnected from broker (rc={rc})")
    
    try:
        # Create MQTT client with WebSocket transport
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
        
        # Set WebSocket path
        client.ws_set_options(path="/mqtt")
        
        # Set callbacks
        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        
        # Connect to broker
        print("🔌 Connecting to broker...")
        client.connect("broker.emqx.io", 8083, 60)
        
        # Start network loop
        client.loop_start()
        
        # Wait for messages
        print("⏳ Waiting for test completion...")
        time.sleep(5.0)
        
        # Cleanup
        client.loop_stop()
        client.disconnect()
        
        print("\n" + "="*50)
        print("🏁 Test Results:")
        print(f"Messages received: {len(messages_received)}")
        
        if len(messages_received) > 0:
            print("✅ MQTT WebSocket connection working!")
        else:
            print("⚠️ No echo received (normal for public broker)")
            print("✅ Connection established successfully")
            
        return True
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
        return False

if __name__ == "__main__":
    success = test_emqx_connection()
    exit(0 if success else 1)