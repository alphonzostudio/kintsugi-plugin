#!/bin/bash
# Build and run standalone plugin

echo "🔨 Building standalone plugin..."
cmake --build build --config Debug --target kintsugi_Standalone -- -j8

if [ $? -eq 0 ]; then
    echo "✅ Build successful! Launching standalone app..."
    open "build/kintsugi_artefacts/Debug/Standalone/Kintsugi.app"
else
    echo "❌ Build failed!"
    exit 1
fi
