# Web Play Synth

A browser-based modular virtual-analog synthesizer. The first instrument is a dual-oscillator subtractive voice with a mixer, resonant low-pass filter, dedicated filter and amplifier ADSR envelopes, and a patchable signal-flow view.

## Browser versions

- `/` is the primary browser synthesizer.
- `/legacy/` preserves the original Web Audio implementation as a separately addressable compatibility version. Its existing UI, synthesis engine, and browser-only features remain intact while the shared C++/WebAssembly browser engine is developed and validated.

Both entry points are built and served by the same static deployment. The legacy entry is not a redirect and does not depend on an external service.

## Native VST3 plugin builds

The native JUCE project produces a VST3 plugin and a Standalone application from the same source code. GitHub Actions builds and validates all supported desktop targets:
