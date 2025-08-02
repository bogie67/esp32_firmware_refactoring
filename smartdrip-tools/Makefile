# SmartDrip Security1 Testing Tools
# Makefile for Security1 BLE and MQTT testing

.PHONY: help install test-ble test-mqtt test-all test-connection clean setup

# Default target
help:
	@echo "🧪 SmartDrip Security1 Testing Tools"
	@echo "===================================="
	@echo ""
	@echo "Setup Commands:"
	@echo "  install        - Install all dependencies in virtual environment"
	@echo "  setup          - Alias for install"
	@echo ""
	@echo "Testing Commands:"
	@echo "  test-connection - Quick MQTT broker connection test"
	@echo "  test-ble       - Test BLE Security1 handshake"
	@echo "  test-mqtt      - Test MQTT Security1 handshake"
	@echo "  test-all       - Test both BLE and MQTT"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean          - Remove virtual environment and cache"
	@echo ""
	@echo "Configuration (override with environment variables):"
	@echo "  BLE_DEVICE     - BLE device name (default: ESP32_Security1)"
	@echo "  MQTT_BROKER    - MQTT broker URL (default: ws://broker.emqx.io:8083/mqtt)"
	@echo "  MQTT_TOPIC     - MQTT topic prefix (default: security1/esp32)"
	@echo "  POP            - Proof of Possession string (default: test_pop_12345)"
	@echo ""
	@echo "Examples:"
	@echo "  make install"
	@echo "  make test-connection"
	@echo "  make test-ble BLE_DEVICE='MyESP32'"
	@echo "  make test-mqtt MQTT_TOPIC='smartdrip/device123'"
	@echo "  make test-mqtt POP='A0B1C2D3E4F5'  # Use MAC-based PoP"

# Configuration variables
PYTHON := python3
VENV_DIR := .venv
ACTIVATE := $(VENV_DIR)/bin/activate

# Test configuration
BLE_DEVICE ?= ESP32_Security1
MQTT_BROKER ?= ws://broker.emqx.io:8083/mqtt
MQTT_TOPIC ?= security1/esp32
POP ?= test_pop_12345

# Installation
install: $(VENV_DIR)
	@echo "📦 Installing SmartDrip Security1 Testing Tools..."
	@. $(ACTIVATE) && pip install --upgrade pip
	@. $(ACTIVATE) && pip install -r requirements.txt
	@echo "✅ Installation complete!"
	@echo ""
	@echo "🚀 Quick start:"
	@echo "  make test-connection    # Test MQTT connection"
	@echo "  make test-mqtt          # Test MQTT Security1"
	@echo "  make test-ble           # Test BLE Security1"

setup: install

$(VENV_DIR):
	@echo "🔧 Creating virtual environment..."
	@$(PYTHON) -m venv $(VENV_DIR)
	@echo "📋 Virtual environment created at $(VENV_DIR)"

# Testing commands
test-connection: $(VENV_DIR)
	@echo "🌐 Testing MQTT WebSocket connection..."
	@echo "Broker: $(MQTT_BROKER)"
	@. $(ACTIVATE) && python test_mqtt_connection.py

test-ble: $(VENV_DIR)
	@echo "🔵 Testing BLE Security1 handshake..."
	@echo "Device: $(BLE_DEVICE)"
	@. $(ACTIVATE) && python test_security1.py \
		--transport ble \
		--device "$(BLE_DEVICE)" \
		--pop "$(POP)" \
		--verbose

test-mqtt: $(VENV_DIR)
	@echo "🟠 Testing MQTT Security1 handshake..."
	@echo "Broker: $(MQTT_BROKER)"
	@echo "Topic: $(MQTT_TOPIC)"
	@. $(ACTIVATE) && python test_security1.py \
		--transport mqtt \
		--broker "$(MQTT_BROKER)" \
		--topic "$(MQTT_TOPIC)" \
		--pop "$(POP)" \
		--verbose

test-all: $(VENV_DIR)
	@echo "🔵🟠 Testing both BLE and MQTT Security1..."
	@echo "BLE Device: $(BLE_DEVICE)"
	@echo "MQTT Broker: $(MQTT_BROKER)"
	@echo "MQTT Topic: $(MQTT_TOPIC)"
	@. $(ACTIVATE) && python test_security1.py \
		--transport both \
		--device "$(BLE_DEVICE)" \
		--broker "$(MQTT_BROKER)" \
		--topic "$(MQTT_TOPIC)" \
		--pop "$(POP)" \
		--verbose

# Interactive testing (prompts for configuration)
test-interactive: $(VENV_DIR)
	@echo "🎯 Interactive Security1 Testing"
	@echo "Choose transport:"
	@echo "1) BLE only"
	@echo "2) MQTT only"
	@echo "3) Both BLE and MQTT"
	@read -p "Enter choice (1-3): " choice; \
	case $$choice in \
		1) make test-ble;; \
		2) make test-mqtt;; \
		3) make test-all;; \
		*) echo "❌ Invalid choice";; \
	esac

# Development helpers
test-crypto: $(VENV_DIR)
	@echo "🔐 Testing cryptography functions..."
	@. $(ACTIVATE) && python -c "\
import sys; \
sys.path.append('.'); \
from test_security1 import Security1Tester; \
tester = Security1Tester(); \
print('🔑 Generating X25519 keypair...'); \
pub_key = tester.generate_client_keypair(); \
print(f'Public key: {pub_key.hex()}'); \
print('✅ Cryptography test passed!')"

deps-check: $(VENV_DIR)
	@echo "📋 Checking dependencies..."
	@. $(ACTIVATE) && python -c "\
try: import bleak; print('✅ BLE support (bleak) available')\
except ImportError: print('❌ BLE support not available'); \
try: import paho.mqtt.client as mqtt; print('✅ MQTT support (paho-mqtt) available')\
except ImportError: print('❌ MQTT support not available'); \
try: import websocket; print('✅ WebSocket support available')\
except ImportError: print('❌ WebSocket support not available'); \
try: from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey; print('✅ Cryptography support available')\
except ImportError: print('❌ Cryptography support not available')"

# Maintenance
clean:
	@echo "🧹 Cleaning up..."
	@rm -rf $(VENV_DIR)
	@rm -rf __pycache__
	@rm -rf *.pyc
	@find . -name "*.pyc" -delete
	@find . -name "__pycache__" -delete
	@echo "✅ Cleanup complete"

# Help for specific tests
help-ble:
	@echo "🔵 BLE Security1 Testing Help"
	@echo "============================="
	@echo ""
	@echo "Prerequisites:"
	@echo "- ESP32 device with Security1 firmware flashed"
	@echo "- Device advertising as 'ESP32_Security1' (or custom name)"
	@echo "- Bluetooth enabled on test machine"
	@echo ""
	@echo "Usage:"
	@echo "  make test-ble                           # Use default device name"
	@echo "  make test-ble BLE_DEVICE='MyESP32'     # Custom device name"
	@echo ""
	@echo "BLE Services:"
	@echo "  Handshake: FF50-FF52 (X25519 key exchange)"
	@echo "  Data:      FF00-FF02 (encrypted communication)"

help-mqtt:
	@echo "🟠 MQTT Security1 Testing Help"
	@echo "=============================="
	@echo ""
	@echo "Prerequisites:"
	@echo "- ESP32 device connected to WiFi"
	@echo "- ESP32 configured for MQTT broker: $(MQTT_BROKER)"
	@echo "- Internet connection for WebSocket MQTT"
	@echo ""
	@echo "Usage:"
	@echo "  make test-mqtt                                    # Use default broker"
	@echo "  make test-mqtt MQTT_BROKER='ws://my.broker.com'  # Custom broker"
	@echo "  make test-mqtt MQTT_TOPIC='smartdrip/device123'  # Custom topic"
	@echo ""
	@echo "MQTT Topics:"
	@echo "  Handshake: {topic}/handshake/request|response"
	@echo "  Data:      {topic}/data/request|response"

# Development workflow
dev-workflow: install test-connection test-crypto deps-check
	@echo "🎉 Development environment ready!"
	@echo ""
	@echo "Next steps:"
	@echo "1. Flash ESP32 with Security1 firmware"
	@echo "2. Run: make test-ble"
	@echo "3. Run: make test-mqtt"
	@echo "4. Run: make test-all"