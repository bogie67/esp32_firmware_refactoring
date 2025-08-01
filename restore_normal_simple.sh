#!/bin/bash

echo "🔄 Restoring normal firmware"

if [ -f "main/app_main_normal.c" ]; then
    cp main/app_main_normal.c main/app_main.c
    echo "✅ Normal app_main.c restored"
else
    echo "⚠️  No backup found - using current app_main.c"
fi

echo "🔨 Building normal firmware"
. /Users/bogie/esp/esp-idf/export.sh && idf.py build

if [ $? -eq 0 ]; then
    echo "✅ Normal firmware build successful"
    echo "🚀 Flash with: idf.py flash monitor"
else
    echo "❌ Normal firmware build failed"
fi