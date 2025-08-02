#!/bin/bash

# Script per generare compile_commands.json per tutti i test_apps
# Questo risolve le linee rosse in VSCode

echo "🔧 Generazione IntelliSense per tutti i test_apps..."

. /Users/bogie/esp/esp-idf/export.sh

COMPONENTS=("codec" "wifi" "solenoid" "schedule" "transport_mqtt" "cmd_proc" "transport_ble")

for comp in "${COMPONENTS[@]}"; do
    echo "📦 Processando componente: $comp"
    cd "/Users/bogie/Progetti/Philla/Clarabella/claude_code/esp32_firmware_refactoring/components/$comp/test_apps"
    
    if [ -f "CMakeLists.txt" ]; then
        echo "  🔨 Building per generare compile_commands.json..."
        idf.py build > /dev/null 2>&1
        
        if [ -f "build/compile_commands.json" ]; then
            echo "  ✅ compile_commands.json generato con successo"
        else
            echo "  ❌ Errore nella generazione compile_commands.json"
        fi
    else
        echo "  ⚠️  CMakeLists.txt non trovato, saltando..."
    fi
    
    cd - > /dev/null
done

echo "🎉 Processo completato! Riavvia VSCode per vedere i cambiamenti."