#!/bin/bash

# Script per pulire tutte le directory build dei test_apps dei componenti
# Questo libera spazio e forza la rigenerazione dei compile_commands.json

echo "🧹 Pulizia build directories dei test_apps..."

COMPONENTS=(
    "protocol_types"
    "codec" 
    "wifi" 
    "solenoid" 
    "schedule" 
    "transport_mqtt" 
    "transport_ble"
    "cmd_proc"
)

TOTAL_CLEANED=0

for comp in "${COMPONENTS[@]}"; do
    TEST_BUILD_DIR="/Users/bogie/Progetti/Philla/Clarabella/claude_code/esp32_firmware_refactoring/components/$comp/test_apps/build"
    
    if [ -d "$TEST_BUILD_DIR" ]; then
        echo "📦 Pulizia build dir per componente: $comp"
        
        # Calcola dimensione prima della pulizia
        if command -v du >/dev/null 2>&1; then
            SIZE_BEFORE=$(du -sh "$TEST_BUILD_DIR" 2>/dev/null | cut -f1)
            echo "  📏 Dimensione prima: $SIZE_BEFORE"
        fi
        
        rm -rf "$TEST_BUILD_DIR"
        
        if [ $? -eq 0 ]; then
            echo "  ✅ Build directory rimossa con successo"
            ((TOTAL_CLEANED++))
        else
            echo "  ❌ Errore durante la rimozione"
        fi
    else
        echo "📦 Componente $comp: nessuna build directory trovata"
    fi
    
    echo ""
done

# Pulizia anche build principale se esiste
MAIN_BUILD_DIR="/Users/bogie/Progetti/Philla/Clarabella/claude_code/esp32_firmware_refactoring/build"
if [ -d "$MAIN_BUILD_DIR" ]; then
    echo "🏠 Pulizia build directory principale..."
    
    if command -v du >/dev/null 2>&1; then
        SIZE_BEFORE=$(du -sh "$MAIN_BUILD_DIR" 2>/dev/null | cut -f1)
        echo "  📏 Dimensione prima: $SIZE_BEFORE"
    fi
    
    rm -rf "$MAIN_BUILD_DIR"
    
    if [ $? -eq 0 ]; then
        echo "  ✅ Build directory principale rimossa"
        ((TOTAL_CLEANED++))
    else
        echo "  ❌ Errore durante la rimozione"
    fi
fi

echo ""
echo "🎉 Pulizia completata!"
echo "📊 Directory pulite: $TOTAL_CLEANED"
echo ""
echo "💡 Per rigenerare i build e compile_commands.json usa:"
echo "   ./generate_intellisense.sh"