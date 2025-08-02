# 🧪 BLE Back-pressure Testing Guide

## Test automatizzato

Ho creato uno script completo per testare la logica di back-pressure implementata nel transport BLE.

### Uso Rapido

```bash
cd /Users/bogie/Progetti/Philla/Clarabella/claude_code/esp32_firmware_refactoring/smartdrip-tools

# Test completo (10-15 minuti)
./test_backpressure.sh

# Test rapido (5 minuti)
./test_backpressure.sh --quick

# Test con output dettagliato
./test_backpressure.sh --verbose
```

### Test Inclusi

#### 🔧 **Test Base**
1. **Basic BLE Command**: Comando semplice per verificare funzionalità base
2. **Chunking Test**: Payload grande (800 bytes) per testare chunking + back-pressure
3. **Forced Chunking**: Chunking forzato su payload piccolo

#### ⚡ **Test Stress**
4. **Multiple Commands**: 5-10 comandi rapidi per stressare il sistema
5. **Large Payload Stress**: Payload 1200 bytes × 3 ripetizioni
6. **Rapid Fire**: Comandi paralleli per triggerare circuit breaker

#### 🔄 **Test Recovery**
7. **Recovery Test**: Verifica recupero dopo stress
8. **Variable Payload**: Test con dimensioni diverse (200, 500, 1000 bytes)

### Log da Monitorare

Durante i test, osserva i log ESP32 per:

**✅ Comportamenti Attesi:**
```
📦 Response chunked into X parts, frame_id=Y
✅ Chunk X/Y sent with back-pressure (attempt Z)
📈 Back-pressure failure recorded: retry=X, consecutive=Y, delay=Zms
✅ Back-pressure recovery: succeeded after X retries
⚠️ Mbuf pool exhausted - chunk X/Y
⛔ Circuit breaker OPEN: X consecutive failures
```

**❌ Problemi da Investigare:**
- Timeout senza retry
- Memory leak warnings
- Circuit breaker che non si resetta
- Chunk duplicati o persi

### Interpretazione Risultati

**🎉 Successo Completo:**
- Tutti i test passano
- Back-pressure si attiva sotto stress
- Recovery automatico funziona
- Chunking gestisce payload grandi

**⚠️ Successo Parziale:**
- Test base passano
- Alcuni stress test falliscono
- → Sistema funziona ma limiti di performance

**❌ Problemi Critici:**
- Test base falliscono
- Nessuna attivazione back-pressure
- → Rivedere implementazione

### Test Manuali Aggiuntivi

#### Test Disconnessione/Riconnessione
```bash
# 1. Avvia comando lungo
python send_ble_command.py --op syncSchedule --large-payload 1500 &

# 2. Durante esecuzione, disconnetti BLE dal telefono
# 3. Riconnetti
# 4. Verifica recovery automatico
```

#### Test Saturazione Mbuf
```bash
# Saturazione pool mbuf con comandi paralleli
for i in {1..20}; do
    python send_ble_command.py --op wifiScan --no-notify &
done
wait
```

### File di Output

Lo script genera:
- **Console output**: Risultati real-time colorati
- **Log file**: `/tmp/ble_backpressure_test_YYYYMMDD_HHMMSS.log`
- **Summary**: Statistiche finali con tasso di successo

### Debugging

Se i test falliscono:

1. **Verifica connessione BLE**:
   ```bash
   python send_ble_command.py --op wifiScan --verbose
   ```

2. **Controlla log ESP32**: 
   - Cerca pattern di error
   - Verifica stato back-pressure
   - Controlla memory usage

3. **Test incrementale**:
   ```bash
   # Test singoli invece della suite completa
   ./test_backpressure.sh --quick --verbose
   ```

### Metriche di Performance

**Target di Performance:**
- Test base: < 30 secondi
- Chunking: < 45 secondi  
- Stress test: < 60 secondi
- Tasso successo: > 90%

**Comportamento Back-pressure Atteso:**
- Delay iniziale: 50ms
- Crescita esponenziale: 100ms → 200ms → 400ms...
- Circuit breaker: Dopo 10 fallimenti consecutivi
- Recovery: Reset automatico su successo