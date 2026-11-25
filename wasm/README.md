# Portable engine WebAssembly target

This directory builds the framework-neutral C++ synthesizer engine without JUCE.
The target is deliberately a separate CMake project: the browser build does not
fetch JUCE or compile plugin/editor code.

## Build

With a native C++17 compiler:

```sh
cmake -S wasm -B build/wasm-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-native --parallel
ctest --test-dir build/wasm-native --output-on-failure
```

With the Emscripten SDK activated in the shell:

```sh
emcmake cmake -S wasm -B build/wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel
```

The Emscripten build emits `build/wasm/webplaysynth_engine.wasm`. It is a
standalone WebAssembly module with no generated JavaScript glue and no virtual
filesystem. The browser host owns memory allocation and passes pointers into
linear memory through the versioned C ABI in `WasmBridge.h`.

## ABI version 1

- `wps_abi_version()`: returns 1.
- `wps_parameter_count()`: returns the exact version-1 engine parameter count.
- `wps_event_size()`: returns the 20-byte event record size.
- `wps_prepare(sampleRate, maxFrames, outputChannels)`: prepares mono/stereo output.
- `wps_render(...)`: renders one block from a parameter snapshot and ordered events.
- `wps_reset(mode)`: 0 voices, 1 effects, 2 all.
- `wps_release()`: releases the prepared engine.
- `wps_is_prepared()`: reports lifecycle state.

The event record contains five 32-bit fields at byte offsets 0, 4, 8, 12, and 16:
`type`, `frameOffset`, `channel`, `note`, and `value`. Event type values
are note-on 0, note-off 1, pitch bend 2, channel pressure 3, polyphonic key
pressure 4, sustain 5, all-notes-off 6, all-sound-off 7, and reset-controllers 8.
Offsets and ordering are validated by the shared engine. The browser host must
keep parameter, event, and output regions in linear memory alive for each call.

The bridge is a transport layer only. It does not add another DSP implementation,
change parameter semantics, allocate memory in the render call, or claim browser
UI integration. The next browser-host step can use this ABI from an
AudioWorklet while keeping the existing React controls and patch/state behavior.
