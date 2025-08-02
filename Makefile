# ESP32 Security1 Firmware Development Makefile

.PHONY: help build flash monitor clean test-ble test-mqtt test-all setup-tests

# Default target
help:
	@echo "🔧 ESP32 Security1 Firmware Development"
	@echo "========================================"
	@echo ""
	@echo "ESP-IDF Commands:"
	@echo "  build          - Build the firmware"
	@echo "  flash          - Flash firmware to ESP32"
	@echo "  monitor        - Monitor serial output"
	@echo "  clean          - Clean build artifacts"
	@echo "  menuconfig     - Configure project settings"
	@echo ""
	@echo "Security1 Testing:"
	@echo "  setup-tests    - Setup Python test environment"
	@echo "  test-ble       - Test BLE Security1 handshake"
	@echo "  test-mqtt      - Test MQTT Security1 handshake"
	@echo "  test-all       - Test both BLE and MQTT"
	@echo ""
	@echo "Usage Examples:"
	@echo "  make build"
	@echo "  make flash && make monitor"
	@echo "  make test-ble DEVICE=ESP32_Security1"
	@echo "  make test-mqtt BROKER=192.168.1.100"

# ESP-IDF Commands
build:
	@echo "🔨 Building ESP32 firmware..."
	. $(HOME)/esp/esp-idf/export.sh && idf.py build

flash:
	@echo "⚡ Flashing ESP32..."
	. $(HOME)/esp/esp-idf/export.sh && idf.py flash

monitor:
	@echo "📺 Starting serial monitor..."
	. $(HOME)/esp/esp-idf/export.sh && idf.py monitor

clean:
	@echo "🧹 Cleaning build artifacts..."
	. $(HOME)/esp/esp-idf/export.sh && idf.py fullclean

menuconfig:
	@echo "⚙️ Opening configuration menu..."
	. $(HOME)/esp/esp-idf/export.sh && idf.py menuconfig

# Security1 Testing
DEVICE ?= ESP32_Security1
BROKER ?= ws://broker.emqx.io:8083/mqtt
TOPIC ?= security1/esp32

setup-tests:
	@echo "📦 Setting up Security1 test environment..."
	@if [ ! -d "venv" ]; then \
		echo "Creating virtual environment..."; \
		python3 -m venv venv; \
	fi
	@echo "Installing dependencies..."
	@. venv/bin/activate && pip install -r requirements.txt
	@echo "✅ Test environment ready!"

test-ble: setup-tests
	@echo "🔵 Testing BLE Security1 handshake..."
	@echo "Device: $(DEVICE)"
	@. venv/bin/activate && python test_security1.py --transport ble --device "$(DEVICE)" --verbose

test-mqtt: setup-tests  
	@echo "🟠 Testing MQTT Security1 handshake..."
	@echo "Broker: $(BROKER)"
	@echo "Topic: $(TOPIC)"
	@. venv/bin/activate && python test_security1.py --transport mqtt --broker "$(BROKER)" --topic "$(TOPIC)" --verbose

test-all: setup-tests
	@echo "🔵🟠 Testing both BLE and MQTT Security1..."
	@echo "Device: $(DEVICE)"
	@echo "Broker: $(BROKER)"
	@. venv/bin/activate && python test_security1.py --transport both --device "$(DEVICE)" --broker "$(BROKER)" --topic "$(TOPIC)" --verbose

# Development workflow
dev: build flash monitor

# Test workflow after firmware changes
test-dev: build flash
	@echo "⏳ Waiting for ESP32 to boot..."
	@sleep 5
	@make test-all

# Clean everything including test environment
clean-all: clean
	@echo "🧹 Cleaning test environment..."
	@rm -rf venv
	@echo "✅ Everything cleaned!"