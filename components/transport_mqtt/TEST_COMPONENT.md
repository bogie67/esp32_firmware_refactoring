# Transport MQTT Component Testing

## ✅ Test Suite Completa Implementata

Il componente `transport_mqtt` dispone di tre livelli di testing per garantire qualità e affidabilità:

1. **Unit Tests** (`test/`) - Test unitari veloci con mock
2. **Test Apps** (`test_apps/`) - App ESP32 per test manuali
3. **E2E Tests** (`e2e_tests/`) - Test end-to-end con broker reale

## 🧪 1. Unit Tests (`test/`) - Mock-Based

### Mock MQTT Client
- **`mocks/mock_mqtt_client.c/h`** - Simula le funzioni `esp_mqtt_client_*`
- Stato controllabile per test deterministici
- Funzioni helper per simulare eventi MQTT
- Supporto per verificare chiamate e stato del client

### Test API Transport (`test_transport_api.c`)
- ✅ Inizializzazione con queue valide/nulle
- ✅ Lifecycle completo (init → start → stop → cleanup)  
- ✅ Stato connessione con eventi simulati
- ✅ Gestione errori e edge cases

### Test Message Routing (`test_message_routing.c`)
- ✅ Comandi JSON decodificati e inoltrati alla queue
- ✅ Filtering per origin (solo ORIGIN_MQTT processati)
- ✅ Risposte pubblicate su topic MQTT
- ✅ Gestione payload e memory management
- ✅ JSON malformato ignorato correttamente

### Test MQTT Events (`test_mqtt_events.c`)
- ✅ Eventi connessione/disconnessione aggiornano stato
- ✅ Cicli connect/disconnect multipli
- ✅ Eventi su topic corretti/sbagliati
- ✅ Comportamento sotto stress (eventi rapidi)

## 🏗️ 2. Test Apps (`test_apps/`) - Hardware Integration

### Applicazione Test ESP32
- **`main/test_transport_mqtt_main.c`** - App standalone per test manuali
- Test su hardware ESP32 reale con WiFi e MQTT
- Debug interattivo tramite monitor seriale
- Verifica integrazione con sistema completo

## 🌐 3. E2E Tests (`e2e_tests/`) - Real-World Testing

### Test Python con Broker Reale
- **`test_basic_commands.py`** - Comandi base (ping, LED, WiFi info)
- **`test_stress.py`** - Test di carico e resilienza
- **`test_runner.py`** - Automazione flash + test
- **`docker-compose.yml`** - Broker MQTT locale per test isolati

## 📁 Struttura File Completa

```
components/transport_mqtt/
├── test/                          # Unit tests con mock
│   ├── CMakeLists.txt
│   ├── test_main.c
│   ├── test_transport_api.c
│   ├── test_message_routing.c
│   ├── test_mqtt_events.c
│   └── mocks/
│       ├── mock_mqtt_client.c
│       └── mock_mqtt_client.h
├── test_apps/                     # ESP32 test application
│   ├── CMakeLists.txt
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── test_transport_mqtt_main.c
│   ├── sdkconfig
│   └── sdkconfig.defaults
├── e2e_tests/                     # End-to-end Python tests
│   ├── requirements.txt
│   ├── conftest.py
│   ├── test_basic_commands.py
│   ├── test_stress.py
│   ├── test_runner.py
│   ├── docker-compose.yml
│   ├── Dockerfile.test
│   └── Makefile
└── TEST_COMPONENT.md              # Questo documento
```

## 🚀 Come Eseguire i Test

### 1. Unit Tests (Veloci - Raccomandati)

#### Test Host-Based (Sviluppo)
```bash
# Build per host Linux/macOS (molto veloce)
cd components/transport_mqtt/test
idf.py --target linux build && ./build/test_transport_mqtt.elf

# Output atteso:
# Unity Test Summary: 24 Tests 0 Failures 0 Ignored
```

#### Test su ESP32 Hardware
```bash
# Build e flash su ESP32
cd components/transport_mqtt/test
idf.py build flash monitor

# Output atteso:
# Running transport_mqtt tests...
# [PASS] test_transport_mqtt_init_with_valid_queues
# [PASS] test_mqtt_command_reaches_queue
# [PASS] test_mqtt_connected_event_updates_state
# ...
# Unity Test Summary: 24 Tests 0 Failures 0 Ignored
```

### 2. Test Apps (Debug Manuale)
```bash
# Build e flash test app
cd components/transport_mqtt/test_apps
idf.py build flash monitor

# Interagisci tramite monitor seriale per test manuali
```

### 3. E2E Tests (Validazione Completa)
```bash
# Setup e run automatico
cd components/transport_mqtt/e2e_tests
make install
make test-all

# O run step-by-step
pip install -r requirements.txt
python test_runner.py --port /dev/ttyUSB0

# O solo test senza flash
python test_runner.py --no-flash
```

## 📊 Caratteristiche Test Levels

### 🧪 Unit Tests (`test/`)
| Caratteristica | Valore |
|----------------|--------|
| **Velocità** | ⚡ <1 secondo |
| **Dipendenze** | 🎭 Solo mock |
| **Copertura** | 🔧 Logica componente |
| **CI/CD** | ✅ Perfetto |
| **Debug** | 🔍 Facile |

### 🏗️ Test Apps (`test_apps/`)
| Caratteristica | Valore |
|----------------|--------|
| **Velocità** | 🐌 30-60 secondi |
| **Dipendenze** | 🔌 ESP32 + WiFi + MQTT |
| **Copertura** | 🌐 Integrazione completa |
| **CI/CD** | ⚠️ Con hardware |
| **Debug** | 🕵️ Hardware reale |

### 🌐 E2E Tests (`e2e_tests/`)
| Caratteristica | Valore |
|----------------|--------|
| **Velocità** | ⏱️ 60-120 secondi |
| **Dipendenze** | 🌍 Sistema completo |
| **Copertura** | 🎯 Scenario utente |
| **CI/CD** | 🤖 Automatizzabile |
| **Debug** | 📈 Performance reale |

## 🎯 Quando Usare Quale Test

### Sviluppo Quotidiano → **Unit Tests**
```bash
# Ciclo veloce: modifica → test → fix
cd components/transport_mqtt/test
idf.py --target linux build && ./build/test_transport_mqtt.elf
```

### Debug Hardware → **Test Apps**
```bash
# Debug step-by-step su ESP32 reale
cd components/transport_mqtt/test_apps
idf.py build flash monitor
```

### Validazione Release → **E2E Tests**
```bash
# Test completi prima del merge
cd components/transport_mqtt/e2e_tests
make test-all
```

## 🎭 Mock MQTT Client (Unit Tests)

Il mock client sostituisce le funzioni ESP-IDF MQTT per test deterministici:

```c
// Reset stato mock prima di ogni test
void mock_mqtt_reset(void);

// Simulazione eventi MQTT controllati
void mock_mqtt_simulate_connected(void);
void mock_mqtt_simulate_disconnected(void);  
void mock_mqtt_simulate_data(const char *topic, const char *data, int len);

// Verifica stato per assertions
bool mock_mqtt_is_started(void);
bool mock_mqtt_is_connected(void);
const char* mock_mqtt_get_last_published_topic(void);
const char* mock_mqtt_get_last_published_data(void);
```

## 🤖 Integrazione CI/CD

### GitHub Actions Pipeline
```yaml
name: Transport MQTT Tests
on: [push, pull_request]
jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v3
    - name: Setup ESP-IDF
      uses: espressif/esp-idf-action@v1
    - name: Run Unit Tests
      run: |
        cd components/transport_mqtt/test
        idf.py --target linux build
        ./build/test_transport_mqtt.elf
        
  e2e-tests:
    runs-on: self-hosted  # Con ESP32 collegato
    steps:
    - name: Run E2E Tests
      run: |
        cd components/transport_mqtt/e2e_tests
        make test-all
```

## 📈 Test Coverage Summary

La test suite garantisce copertura completa del componente `transport_mqtt`:

- ✅ **API Functions**: Tutte le funzioni pubbliche testate
- ✅ **Message Flow**: Comando JSON → processing → risposta JSON
- ✅ **Event Handling**: Connect/disconnect/data events
- ✅ **Error Scenarios**: JSON malformato, queue piene, disconnessioni
- ✅ **Memory Management**: Allocazione/deallocazione payload
- ✅ **Origin Filtering**: Separazione traffic BLE vs MQTT
- ✅ **Performance**: Throughput, latency, stress testing
- ✅ **Integration**: Dual transport (BLE + MQTT simultaneo)

---

## 🏁 Quick Start

```bash
# 1. Test veloce durante sviluppo (1 secondo)
cd components/transport_mqtt/test && idf.py --target linux build && ./build/test_transport_mqtt.elf

# 2. Test completo con hardware (2 minuti)  
cd components/transport_mqtt/e2e_tests && make test-all

# 3. Debug manuale interattivo
cd components/transport_mqtt/test_apps && idf.py build flash monitor
```

**Raccomandazione**: Usa **unit tests** per sviluppo rapido, **E2E tests** per validazione finale, **test apps** per debug specifici.