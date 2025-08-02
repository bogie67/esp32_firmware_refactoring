import pytest
import paho.mqtt.client as mqtt
import json
import time
import threading
import queue
import logging
from typing import List, Dict, Any

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class MQTTTestClient:
    """Test MQTT client for E2E testing"""
    
    def __init__(self, broker_host="broker.emqx.io", broker_port=1883):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.client = mqtt.Client()
        self.connected = False
        self.messages = queue.Queue()
        self.subscribed_topics = []
        
        # Setup callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        
    def _on_connect(self, client, userdata, flags, rc):
        """Callback when client connects"""
        if rc == 0:
            self.connected = True
            logger.info(f"✅ Connected to MQTT broker {self.broker_host}:{self.broker_port}")
        else:
            logger.error(f"❌ Failed to connect to MQTT broker: {rc}")
    
    def _on_disconnect(self, client, userdata, rc):
        """Callback when client disconnects"""
        self.connected = False
        logger.info("🔌 Disconnected from MQTT broker")
    
    def _on_message(self, client, userdata, msg):
        """Callback when message received"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            logger.info(f"📨 Received message on {topic}: {payload}")
            
            # Try to parse as JSON
            try:
                data = json.loads(payload)
            except json.JSONDecodeError:
                data = payload
            
            self.messages.put({
                'topic': topic,
                'payload': payload,
                'data': data,
                'timestamp': time.time()
            })
        except Exception as e:
            logger.error(f"❌ Error processing message: {e}")
    
    def connect(self, timeout=10):
        """Connect to MQTT broker"""
        logger.info(f"🔗 Connecting to {self.broker_host}:{self.broker_port}")
        self.client.connect(self.broker_host, self.broker_port, 60)
        self.client.loop_start()
        
        # Wait for connection
        start_time = time.time()
        while not self.connected and (time.time() - start_time) < timeout:
            time.sleep(0.1)
        
        if not self.connected:
            raise ConnectionError(f"Failed to connect to MQTT broker within {timeout}s")
        
        return True
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()
    
    def subscribe(self, topic, qos=0):
        """Subscribe to topic"""
        if not self.connected:
            raise RuntimeError("Not connected to MQTT broker")
        
        result = self.client.subscribe(topic, qos)
        if result[0] == mqtt.MQTT_ERR_SUCCESS:
            self.subscribed_topics.append(topic)
            logger.info(f"📋 Subscribed to {topic}")
        else:
            raise RuntimeError(f"Failed to subscribe to {topic}: {result}")
    
    def publish(self, topic, payload, qos=0, retain=False):
        """Publish message to topic"""
        if not self.connected:
            raise RuntimeError("Not connected to MQTT broker")
        
        if isinstance(payload, dict):
            payload = json.dumps(payload)
        
        result = self.client.publish(topic, payload, qos, retain)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.info(f"📤 Published to {topic}: {payload}")
            return result.mid
        else:
            raise RuntimeError(f"Failed to publish to {topic}: {result.rc}")
    
    def wait_for_message(self, timeout=5, topic_filter=None):
        """Wait for message from ESP32"""
        try:
            message = self.messages.get(timeout=timeout)
            
            if topic_filter and message['topic'] != topic_filter:
                # Put it back and try again
                self.messages.put(message)
                return self.wait_for_message(timeout, topic_filter)
            
            return message
        except queue.Empty:
            return None
    
    def clear_messages(self):
        """Clear message queue"""
        while not self.messages.empty():
            try:
                self.messages.get_nowait()
            except queue.Empty:
                break

@pytest.fixture(scope="session")
def mqtt_broker():
    """Fixture providing MQTT broker info"""
    return {
        "host": "broker.emqx.io",
        "port": 1883,
        "ws_port": 8083
    }

@pytest.fixture
def mqtt_client(mqtt_broker):
    """Fixture providing configured MQTT test client"""
    client = MQTTTestClient(mqtt_broker["host"], mqtt_broker["port"])
    
    # Connect to broker
    client.connect()
    
    # Subscribe to response topic
    client.subscribe("smartdrip/resp")
    
    yield client
    
    # Cleanup
    client.disconnect()

@pytest.fixture
def esp32_topics():
    """ESP32 MQTT topics"""
    return {
        "cmd": "smartdrip/cmd",
        "resp": "smartdrip/resp"
    }

@pytest.fixture(autouse=True)
def test_setup_teardown(mqtt_client):
    """Setup and teardown for each test"""
    # Clear any pending messages before test
    mqtt_client.clear_messages()
    
    yield
    
    # Cleanup after test
    mqtt_client.clear_messages()

# Helper functions for common test patterns
def send_command(mqtt_client, command_id, op, payload=None):
    """Helper to send command to ESP32"""
    cmd = {
        "id": command_id,
        "op": op
    }
    
    if payload is not None:
        cmd["payload"] = payload
    
    mqtt_client.publish("smartdrip/cmd", cmd)
    return cmd

def wait_for_response(mqtt_client, expected_id, timeout=5):
    """Helper to wait for response from ESP32"""
    message = mqtt_client.wait_for_message(timeout, "smartdrip/resp")
    
    if not message:
        return None
    
    if isinstance(message['data'], dict) and message['data'].get('id') == expected_id:
        return message['data']
    
    return None