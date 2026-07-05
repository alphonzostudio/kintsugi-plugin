# Kintsugi — Spectral Freeze / Repair Effect

![Plugin Interface](https://img.shields.io/badge/JUCE-Audio%20Plugin-blue)
![Language](https://img.shields.io/badge/C++-17-red)
![Platform](https://img.shields.io/badge/Platform-macOS-green)
![Status](https://img.shields.io/badge/Status-In%20Progress-yellow)

An audio effect plugin built with JUCE. Kintsugi captures a snapshot of the
live audio spectrum via FFT and blends it back in over the dry signal. Held
that way, the frozen spectrum decays and shimmers over time — like the
gold-veined lacquer repair of Japanese kintsugi pottery, but for sound.

[![More plugins](https://img.shields.io/badge/More%20plugins-dumumub.com-orange?style=for-the-badge)](https://dumumub.com)

## How it works

Kintsugi runs a real-time STFT (short-time Fourier transform) on the input
signal. Pressing **Freeze** latches the current magnitude/phase spectrum;
an inverse FFT reconstructs it continuously and crossfades it against the
live input using the **Freeze Blend** control.

- **Freeze Blend** — dry/frozen crossfade amount
- **Gold Shimmer** — per-bin modulation applied to the frozen spectrum, evoking
  the gold veins of a kintsugi repair
- **Resolution** — FFT size (2048–16384 samples), trading time vs. frequency
  resolution
- **Decay** — how quickly the frozen spectrum fades once captured

## Status

Core DSP and UI are implemented and building cleanly (Standalone, AU, VST3,
CLAP). Not yet tuned by ear in a DAW — parameter ranges and the
shimmer/decay curves are first-pass values. No demo media yet.

## Building

```bash
git submodule update --init --recursive
cmake -B build
./build-plugins.sh   # builds + installs AU/VST3/CLAP
./run.sh              # builds + launches the standalone app
```

Built on the [Pamplejuce](https://github.com/sudara/pamplejuce) JUCE/CMake
template.
