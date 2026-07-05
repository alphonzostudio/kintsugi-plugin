#!/bin/bash
# Build all plugin formats (AU, VST3, CLAP) and install them

echo "🔨 Building all plugin formats..."
cmake --build build --config Debug -- -j8

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful!"
    echo ""
    echo "📦 Installed plugins:"
    echo "   AU:   ~/Library/Audio/Plug-Ins/Components/Kintsugi.component"
    echo "   VST3: ~/Library/Audio/Plug-Ins/VST3/Kintsugi.vst3"
    echo "   CLAP: ~/Library/Audio/Plug-Ins/CLAP/Kintsugi.clap"
    echo ""
    echo "🎸 You can now load them in your DAW!"
else
    echo "❌ Build failed!"
    exit 1
fi
