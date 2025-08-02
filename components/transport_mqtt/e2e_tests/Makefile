# Transport MQTT E2E Tests Makefile

.PHONY: install test test-basic test-stress test-all flash-and-test clean help

# Default Python and pip commands
PYTHON := python3
PIP := pip3

# Test runner script
TEST_RUNNER := $(PYTHON) test_runner.py

# ESP32 settings (override with make ESPPORT=/dev/ttyUSB1)
ESPPORT ?= auto
BUILD_DIR ?= ../../../../build

help:
	@echo "Transport MQTT E2E Tests"
	@echo "========================"
	@echo ""
	@echo "Available targets:"
	@echo "  install         - Install Python dependencies"
	@echo "  test           - Run all E2E tests (no flash)"
	@echo "  test-basic     - Run basic command tests only"
	@echo "  test-stress    - Run stress tests only"
	@echo "  test-all       - Flash ESP32 + run all tests"
	@echo "  flash-and-test - Flash ESP32 and run tests"
	@echo "  flash-only     - Flash ESP32 firmware only"
	@echo "  broker-local   - Start local MQTT broker"
	@echo "  broker-stop    - Stop local MQTT broker"
	@echo "  clean          - Clean up test artifacts"
	@echo ""
	@echo "Environment variables:"
	@echo "  ESPPORT        - ESP32 serial port (default: auto)"
	@echo "  BUILD_DIR      - ESP32 build directory"
	@echo ""
	@echo "Examples:"
	@echo "  make install"
	@echo "  make test-all"
	@echo "  make test-basic ESPPORT=/dev/ttyUSB0"
	@echo "  make flash-and-test"

install:
	@echo "📦 Installing Python dependencies..."
	$(PIP) install -r requirements.txt
	@echo "✅ Dependencies installed"

test:
	@echo "🧪 Running E2E tests (no flash)..."
	$(TEST_RUNNER) --no-flash $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT))

test-basic:
	@echo "🧪 Running basic command tests..."
	$(TEST_RUNNER) --no-flash --tests test_basic_commands.py $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT))

test-stress:
	@echo "🧪 Running stress tests..."
	$(TEST_RUNNER) --no-flash --tests test_stress.py $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT))

test-all: flash-and-test

flash-and-test:
	@echo "🔨 Flashing ESP32 and running all tests..."
	$(TEST_RUNNER) $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT))

flash-only:
	@echo "📤 Flashing ESP32 firmware only..."
	$(TEST_RUNNER) --no-flash --timeout 0 $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT)) || true
	@# We expect this to "fail" since we skip tests

rebuild-and-test:
	@echo "🔨 Rebuilding and testing..."
	$(TEST_RUNNER) --rebuild $(if $(filter-out auto,$(ESPPORT)),--port $(ESPPORT))

# Docker-based testing
test-docker:
	@echo "🐳 Running tests in Docker..."
	docker-compose up --build --abort-on-container-exit
	docker-compose down

broker-local:
	@echo "🌐 Starting local MQTT broker..."
	docker run -d --name mqtt-test-broker \
		-p 1883:1883 -p 8083:8083 -p 18083:18083 \
		emqx/emqx:latest
	@echo "✅ MQTT broker started on localhost:1883"
	@echo "🌐 Dashboard: http://localhost:18083 (admin/public)"

broker-stop:
	@echo "🛑 Stopping local MQTT broker..."
	docker stop mqtt-test-broker || true
	docker rm mqtt-test-broker || true
	@echo "✅ MQTT broker stopped"

# Development helpers
monitor:
	@echo "📟 Monitoring ESP32 serial output..."
	@if [ "$(ESPPORT)" = "auto" ]; then \
		echo "⚠️  Please specify ESPPORT, e.g., make monitor ESPPORT=/dev/ttyUSB0"; \
	else \
		$(PYTHON) -c "import serial; s=serial.Serial('$(ESPPORT)',115200); [print(s.readline().decode().strip()) for _ in iter(int, 1)]"; \
	fi

mqtt-pub:
	@echo "📤 Publishing test message to ESP32..."
	@command -v mosquitto_pub >/dev/null 2>&1 || { echo "❌ mosquitto_pub not found. Install with: sudo apt install mosquitto-clients"; exit 1; }
	mosquitto_pub -h broker.emqx.io -t smartdrip/cmd -m '{"id":999,"op":"ping"}'
	@echo "✅ Test message sent"

mqtt-sub:
	@echo "📨 Subscribing to ESP32 responses..."
	@command -v mosquitto_sub >/dev/null 2>&1 || { echo "❌ mosquitto_sub not found. Install with: sudo apt install mosquitto-clients"; exit 1; }
	mosquitto_sub -h broker.emqx.io -t smartdrip/resp

# Quick test sequence for development
quick-test: install test-basic

# Full test sequence for CI
ci-test: install test-all

clean:
	@echo "🧹 Cleaning up..."
	find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete 2>/dev/null || true
	find . -type d -name ".pytest_cache" -exec rm -rf {} + 2>/dev/null || true
	docker-compose down 2>/dev/null || true
	@echo "✅ Cleanup completed"

# Show test status
status:
	@echo "📊 Test Environment Status"
	@echo "=========================="
	@echo "Python: $(shell $(PYTHON) --version 2>&1)"
	@echo "Pip packages:"
	@$(PIP) list | grep -E "(paho-mqtt|pytest|pyserial)" || echo "  ⚠️  Required packages not installed"
	@echo ""
	@if [ "$(ESPPORT)" != "auto" ]; then \
		echo "ESP32 Port: $(ESPPORT)"; \
		if [ -e "$(ESPPORT)" ]; then \
			echo "  ✅ Port exists"; \
		else \
			echo "  ❌ Port not found"; \
		fi; \
	else \
		echo "ESP32 Port: auto-detect"; \
	fi
	@echo ""
	@echo "Build Directory: $(BUILD_DIR)"
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "  ✅ Build directory exists"; \
	else \
		echo "  ❌ Build directory not found"; \
	fi
	@echo ""
	@echo "MQTT Broker: broker.emqx.io:1883"
	@nc -z broker.emqx.io 1883 2>/dev/null && echo "  ✅ Broker reachable" || echo "  ⚠️  Broker not reachable"