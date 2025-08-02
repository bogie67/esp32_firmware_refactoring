#!/bin/bash
# Security1 Test Runner Script

set -e

echo "🔧 Setting up Security1 Test Environment"

# Check if virtual environment exists
if [ ! -d "venv" ]; then
    echo "📦 Creating virtual environment..."
    python3 -m venv venv
fi

# Activate virtual environment
echo "🔌 Activating virtual environment..."
source venv/bin/activate

# Install dependencies
echo "📥 Installing dependencies..."
pip install -r requirements.txt

echo ""
echo "🧪 Security1 Test Suite Ready!"
echo "================================"
echo ""

# Ask user for test mode
echo "Select test mode:"
echo "1) BLE only"
echo "2) MQTT only  
echo "3) Both BLE and MQTT"
echo ""
read -p "Enter choice (1-3): " choice

case $choice in
    1)
        echo "🔵 Running BLE tests..."
        read -p "Enter BLE device name [ESP32_Security1]: " device_name
        device_name=${device_name:-ESP32_Security1}
        python test_security1.py --transport ble --device "$device_name" --verbose
        ;;
    2)
        echo "🟠 Running MQTT tests..."
        read -p "Enter MQTT broker URL [ws://broker.emqx.io:8083/mqtt]: " broker_url
        broker_url=${broker_url:-"ws://broker.emqx.io:8083/mqtt"}
        python test_security1.py --transport mqtt --broker "$broker_url" --verbose
        ;;
    3)
        echo "🔵🟠 Running both BLE and MQTT tests..."
        read -p "Enter BLE device name [ESP32_Security1]: " device_name
        device_name=${device_name:-ESP32_Security1}
        read -p "Enter MQTT broker URL [ws://broker.emqx.io:8083/mqtt]: " broker_url
        broker_url=${broker_url:-"ws://broker.emqx.io:8083/mqtt"}
        python test_security1.py --transport both --device "$device_name" --broker "$broker_url" --verbose
        ;;
    *)
        echo "❌ Invalid choice"
        exit 1
        ;;
esac

echo ""
echo "✅ Test execution completed!"
echo "Check the output above for results."

# Deactivate virtual environment
deactivate