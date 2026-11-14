# Web Play Synth — Native Plugin

This directory contains the native C++ instrument built with JUCE. It is separate from the React/Web Audio application, which remains independently deployable.

## Formats

- VST3
- Standalone application

Audio Unit can be added to the same project for macOS. The first milestone establishes the VST3 build and native engine.

## Build

Requirements: CMake 3.22+, a C++17 compiler, Git, and the platform audio/GUI development packages required by JUCE.

Configure with: cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release

Build with: cmake --build build/plugin --config Release --parallel

The first build fetches JUCE 8.0.6 through CMake FetchContent. The framework version is pinned to keep local builds and CI consistent.

## Native engine

- 16-voice polyphonic MIDI instrument
- Two independently tunable oscillators with sine, triangle, saw, and square waveforms
- PolyBLEP edge correction for saw and square oscillators
- Oscillator levels, octave/semitone tuning, hard sync, and cross modulation
- Resonant low-pass filter with dedicated filter ADSR
- Amplifier ADSR
- LFO pitch and filter modulation
- Global glide enable and adjustable 0.01–2.00 second time
- Audio routing switches and master level
- Host-automatable parameters and DAW session state persistence

The initial editor is intentionally minimal while the native DSP foundation is validated. A purpose-built interface matching the browser instrument follows.

## Licensing

JUCE is dual-licensed. Before distributing binaries, choose and comply with the JUCE licence that applies (including its AGPLv3 or commercial terms). The plugin's distribution terms must be compatible with that choice.
