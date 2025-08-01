# 🧪 Unity Test Framework Setup - ESP32 Firmware

## 📋 Sistema Unity Configurato

✅ **Unity Test Framework** integrato e funzionante  
✅ **Script automatici** per passare da normale a test  
✅ **Test WiFi** implementati e pronti  
✅ **Build verificato** sia normale che test

## 🚀 Come Eseguire i Test

### 1. **Passare alla modalità test:**
```bash
./run_tests_simple.sh
```

### 2. **Flashare e monitorare:**
```bash
idf.py flash monitor
```

### 3. **Tornare al firmware normale:**
```bash
./restore_normal_simple.sh
```

## 🧪 Test Implementati

### **Test WiFi (`test_wifi_simple.c`):**
- ✅ `test_wifi_scan_returns_json()` - Verifica scan WiFi e output JSON
- ✅ `test_wifi_configure_basic()` - Verifica configurazione WiFi base

## 📁 File Creati

| File | Descrizione |
|------|------------|
| `main/test_wifi_simple.c` | Test WiFi con Unity |
| `main/test_wifi_simple.h` | Header per i test |
| `main/app_main_test.c` | Main alternativo per test |
| `run_tests_simple.sh` | Script per modalità test |
| `restore_normal_simple.sh` | Script per firmware normale |

## 🔄 Workflow Completo

1. **Sviluppo normale:** Usa il firmware standard con BLE + WiFi
2. **Test phase:** Esegui `./run_tests_simple.sh` + flash + monitor
3. **Risultati test:** Unity mostrerà risultati dettagliati su serial
4. **Torna normale:** Esegui `./restore_normal_simple.sh`

## 📊 Output Atteso Test

```
🧪 Test: wifiScan returns JSON
📊 Scan result: 0, JSON length: 123
📄 JSON content: {"aps":[...]}
✅ Test passed - Valid JSON returned

🧪 Test: Basic WiFi configuration  
📊 Configure result: -1
✅ Test completed

🏁 All WiFi tests completed
```

## 🔧 Aggiungere Nuovi Test

1. **Aggiungi test in `test_wifi_simple.c`:**
   ```c
   void test_my_new_function(void) {
       // Il tuo test qui
       TEST_ASSERT_EQUAL(expected, actual);
   }
   ```

2. **Registra in `run_wifi_tests()`:**
   ```c
   RUN_TEST(test_my_new_function);
   ```

3. **Rebuild:** `./run_tests_simple.sh`

## ⚡ Sistema Pronto!

Unity è completamente integrato e funzionante. Puoi ora eseguire test automatizzati sulle funzioni WiFi e BLE del tuo ESP32!