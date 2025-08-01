#!/bin/bash

echo "🧪 Switching to test mode"

# Backup del main normale
if [ ! -f "main/app_main_normal.c" ]; then
    cp main/app_main.c main/app_main_normal.c
fi

# Sostituisci con il main di test
cp main/app_main_test.c main/app_main.c

echo "🔨 Building test firmware"
. /Users/bogie/esp/esp-idf/export.sh && idf.py build

if [ $? -eq 0 ]; then
    echo "✅ Test build successful"
    echo "🚀 Flash and monitor with: idf.py flash monitor"
    echo "🧪 Tests will run automatically at startup"
else
    echo "❌ Test build failed"
    # Ripristina il main normale in caso di errore
    cp main/app_main_normal.c main/app_main.c
fi