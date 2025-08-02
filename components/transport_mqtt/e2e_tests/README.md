# Transport MQTT E2E Tests

Test end-to-end completi per il transport MQTT con broker reale e ESP32 hardware.

## 🎯 Obiettivo

Questi test verificano il funzionamento completo del transport MQTT in condizioni reali:
- ESP32 hardware con firmware flashato
- Broker MQTT reale (broker.emqx.io o locale)
- Comunicazione bidirezionale JSON
- Scenari di stress e resilienza

## 📋 Test Implementati

### **Basic Commands** (`test_basic_commands.py`)
- ✅ Ping command
- ✅ LED control (on/off)
- ✅ WiFi info command
- ✅ Solenoid control
- ✅ Invalid command handling
- ✅ Malformed JSON handling
- ✅ Rapid command sequences
- ✅ Long payload support
- ✅ MQTT/BLE coexistence

### **Stress Tests** (`test_stress.py`)
- ✅ High frequency commands (20 cmd/s)
- ✅ Concurrent MQTT clients simulation
- ✅ Message burst patterns
- ✅ Various payload sizes (0-200 bytes)
- ✅ Connection resilience testing
- ✅ Random command sequences

## 🚀 Esecuzione Tests

### **Metodo 1: Script Automatico (Raccomandato)**

```bash
# Installa dipendenze
cd components/transport_mqtt/e2e_tests
pip install -r requirements.txt

# Run completo: flash + test
python test_runner.py

# Solo test (senza flash)
python test_runner.py --no-flash

# Test specifici
python test_runner.py --tests test_basic_commands.py

# Con porta ESP32 specifica
python test_runner.py --port /dev/ttyUSB0
```

### **Metodo 2: Pytest Diretto**

```bash
# Prerequisiti: ESP32 già flashato e connesso
pip install -r requirements.txt

# Run tutti i test
pytest -v

# Test specifici
pytest test_basic_commands.py -v
pytest test_stress.py -v

# Con output dettagliato
pytest -v -s --tb=long
```

### **Metodo 3: Docker Compose (Isolato)**

```bash
# Start test environment con broker locale
docker-compose up --build

# Run test specifici
docker-compose run e2e-tests pytest test_basic_commands.py -v

# Cleanup
docker-compose down
```

## 🔧 Configurazione

### **Broker MQTT**

Default: `broker.emqx.io:1883` (pubblico)

Per broker locale:
```bash
# Start local broker
docker run -d --name mqtt-broker -p 1883:1883 emqx/emqx:latest

# Update conftest.py
# mqtt_broker fixture: "host": "localhost"
```

### **ESP32 Setup**

1. **Hardware**: ESP32 connesso via USB
2. **Firmware**: Build con transport MQTT abilitato
3. **WiFi**: Configurato per rete "BogieMobile"
4. **MQTT**: Configurato per broker corretto

### **Porte Seriali**

Auto-detection supportata per:
- CP210x (ESP32-DevKitC)
- CH340 (NodeMCU, Wemos)
- FTDI (custom boards)

Override manuale:
```bash
python test_runner.py --port /dev/ttyUSB0    # Linux
python test_runner.py --port COM3           # Windows
python test_runner.py --port /dev/cu.SLAB*  # macOS
```

## 📊 Output Test

### **Successo**
```
🧪 Testing ping command...
📤 Published to smartdrip/cmd: {"id": 1, "op": "ping"}
📨 Received message on smartdrip/resp: {"id":1,"status":0,"is_final":true,"payload":null}
✅ Ping response: {'id': 1, 'status': 0, 'is_final': True, 'payload': None}

========================= 8 passed in 45.2s =========================
```

### **Fallimento**
```
❌ Failed to connect to MQTT broker within 10s
FAILED test_basic_commands.py::test_ping_command - ConnectionError: ...

========================= 1 failed in 15.3s =========================
```

## 🔍 Troubleshooting

### **Broker Connection Failed**
```bash
# Test broker manualmente
mosquitto_pub -h broker.emqx.io -t test/topic -m "hello"
mosquitto_sub -h broker.emqx.io -t test/topic
```

### **ESP32 Not Ready**
```bash
# Monitor seriale manualmente
idf.py monitor

# Cercare log:
# "WiFi connesso! IP: x.x.x.x"
# "MQTT connesso al broker"
```

### **Permission Denied (Linux)**
```bash
# Aggiungi user al gruppo dialout
sudo usermod -a -G dialout $USER
logout/login

# O run come root
sudo python test_runner.py
```

## 🎭 Mock vs E2E

| Aspetto | Unit Tests (Mock) | E2E Tests (Reale) |
|---------|-------------------|-------------------|
| **Velocità** | ⚡ <1s | 🐌 30-60s |
| **Affidabilità** | 🎯 100% | 📶 95% (rete) |
| **Copertura** | 🔧 Logica | 🌐 Sistema completo |
| **CI/CD** | ✅ Sempre | ⚠️ Con hardware |
| **Debug** | 🔍 Facile | 🕵️ Complesso |

**Raccomandazione**: Unit tests per sviluppo rapido, E2E per validazione finale.

## 📈 Metriche Performance

Target performance per ESP32:
- **Command Latency**: <500ms (ping)
- **Throughput**: >10 cmd/s sustained
- **Success Rate**: >95% under normal load
- **Memory Usage**: <50KB heap durante test
- **Connection Recovery**: <5s dopo disconnessione

## 🔄 CI/CD Integration

```yaml
# GitHub Actions esempio
- name: E2E MQTT Tests
  run: |
    cd components/transport_mqtt/e2e_tests
    pip install -r requirements.txt
    python test_runner.py --no-flash --timeout 30
  env:
    MQTT_BROKER_HOST: "broker.emqx.io"
```

**Nota**: Tests E2E richiedono hardware ESP32 connesso al runner CI.