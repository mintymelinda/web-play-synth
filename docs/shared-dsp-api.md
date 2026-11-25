# Shared DSP engine API

## Purpose

The portable DSP primitives are independent of JUCE, but the plugin still owns
voice allocation, note lifecycle, parameter snapshots, modulation routing, and
the top-level render loop. This document defines the boundary those responsibilities
will move behind before the same engine is hosted by the VST3 and WebAssembly.

The native JUCE processor remains the behavioral reference during this work. This
API is not permission to change sound, parameter ranges, MIDI semantics, or
existing patch/state formats.

## Boundary and ownership

The shared engine owns synthesis and audio processing:

- voice allocation and per-voice oscillator, envelope, LFO, and filter state;
- note lifecycle and per-channel performance expression after events are supplied;
- modulation evaluation and the ordering of per-sample DSP operations;
- chorus, delay, reverb, and master-output processing;
- deterministic reset and render state.

The host adapter owns host integration:

- parameter registration, host automation, and normalized-to-plain conversion;
- MIDI parsing and conversion to engine events;
- audio bus negotiation and non-owning input/output buffer views;
- plugin state serialization, factory/user patch files, and editor/UI;
- host timing, transport, device selection, and permissions.

The browser adapter will own browser UI, AudioWorklet scheduling, and WebAssembly
memory/buffer bridging. It must not duplicate synthesis or modulation behavior.

## Lifecycle contract

The engine has an explicit lifecycle:

1. Construct in an inert state. Construction must not allocate large audio buffers
   or depend on a host framework.
2. `prepare(ProcessSpec)` establishes sample rate, maximum block size, and channel
   layout. It may allocate/reserve storage and resets time-dependent state.
3. Parameter updates replace the pending parameter snapshot. Updates are bounded
   and allocation-free; the audio thread observes a coherent snapshot at a defined
   block boundary.
4. Events are applied at their sample offsets, in stable input order, before the
   corresponding sample is rendered.
5. `process(AudioBlockView)` processes only the supplied frames and channels. It
   performs no allocation, locking, I/O, or unbounded work.
6. `reset(ResetMode)` clears the selected voice/effect state without changing the
   configured sample rate or silently changing parameter values.
7. `release()` returns the engine to the inert state and may release storage. It
   is not called from the real-time callback.

The API types and their validation rules are implemented in
`plugin/Source/Engine/EngineApi.h`. They are deliberately separate from the
JUCE processor; this step does not yet move rendering or change runtime behavior.

## Framework-neutral types

The public boundary uses fixed-width integer types, plain structs, enums, and
non-owning pointer/length views. It does not expose JUCE types, owning containers,
exceptions, or host-owned objects.

### Process specification

`ProcessSpec` carries sample rate in Hz, maximum expected frames per call,
and input/output channel counts. It accepts finite positive sample rates,
a non-zero maximum block size, zero to two input channels, and one or two
output channels. The initial engine supports mono and stereo output.

### Audio blocks

`AudioBlockView` is a non-owning planar float output view. It requires one or
two channels; for non-empty blocks, the channel table and every channel pointer
must be non-null. Zero-frame output views are valid without backing sample
pointers. Output uses replacing semantics.

`ConstAudioBlockView` is the corresponding read-only input view. A zero-channel
input is valid and represents no connected input. Input must not alias output
unless the adapter explicitly guarantees safe in-place operation.

The engine must reject blocks larger than the prepared maximum and must never
read or write outside the supplied frame/channel extents.

### Events

`EngineEvent` is a compact tagged event with a frame offset, 1-based MIDI
channel, note number, and scalar value. The initial event set is note on/off,
pitch bend, channel pressure, polyphonic key pressure, sustain pedal, all notes
off, all sound off, and reset controllers.

The scalar is normalized: note velocity, pressure, and pedal state use [0, 1];
pitch bend uses [-1, 1]. Note numbers are limited to 0-127 and channels to 1-16.
An event offset must be strictly less than the block frame count. Invalid and
non-finite event values are rejected, not silently clamped. An empty event view
is valid; a non-empty view requires storage. Events are consumed in supplied
array order, including when multiple events share an offset.

The engine receives already-decoded events. MIDI byte parsing and CC-to-parameter
mapping stay in the host adapter until there is a deliberate cross-host contract
for them.

### Parameters

`ParameterSnapshotView` carries a schema version, revision, and non-owning
pointer/count pair of float values in engine-defined order and native units.
Version 1 is the initial schema marker. A non-empty snapshot requires a non-null
value pointer. The engine must reject unknown schema versions and consume/copy
the view at a block boundary; it must not retain the caller's pointer.

The host adapter remains responsible for mapping host parameter IDs/ranges to
the snapshot. The snapshot contains only user-controllable values; derived values
such as frequency ratios, glide coefficients, unison gains, and filter drive gain
are computed by the engine from the snapshot and current process specification.
Atomic publication and the concrete version-1 parameter ordering will be
implemented alongside the engine that consumes this view; this type alone does
not claim to provide thread-safe publication.

### Status and reset

`EngineStatus` defines the outcomes needed by the eventual processing API:
success, unprepared state, invalid specification or views, invalid events,
unsupported block size, and incompatible parameter schema. `ResetMode` selects
voices, effects, or all engine state.

## Determinism and test hooks

The engine must support deterministic offline rendering:

- explicit sample rate and block boundaries;
- explicit ordered event lists;
- explicit parameter snapshots;
- reset to a known state;
- no wall-clock, random-device, or host-global dependencies.

Test-only observability should be compile-time or injected through a narrow
diagnostic interface. It may expose voice counts, phase/state snapshots, and
allocation counters, but must not add branches or locks to the production render
path when disabled.

Golden/reference tests continue to compare complete rendered buffers against the
JUCE implementation. Report signed-error mean, standard deviation, RMS, and
maximum absolute error. Keep current strict tolerances; do not weaken tests to
accommodate the extraction.

## Real-time and memory rules

- No heap allocation, deallocation, mutex acquisition, filesystem access, logging,
  or exception propagation in process or event application.
- All variable-capacity storage is sized in prepare from ProcessSpec and fixed
  product limits.
- Event processing and voice stealing have bounded work.
- Parameter publication must be race-safe without blocking the audio thread.
- Reset and release semantics must not leave stale voices or effect tails.
- Every buffer access is bounded by the supplied view, not merely the prepared
  maximum block size.

## Migration sequence

1. Add and test the framework-neutral API types and lifecycle contract.
2. Move the current voice renderer and voice allocator behind the API without
   changing equations or event timing.
3. Move effect-chain and master-output processing behind the same API.
4. Make the JUCE processor a thin adapter and prove rendered parity plus existing
   MIDI/state/plugin tests.
5. Add a native standalone harness that uses only the portable engine.
6. Compile the portable engine to WebAssembly and connect it to an AudioWorklet.
7. Retire the legacy Web Audio synthesis implementation only after browser audio
   regression coverage exercises the shared engine.

## Explicit non-goals

This API work does not add synthesis features, change presets, alter patch formats,
change the plugin editor, implement microphone pitch tracking, or target sonic
equivalence with the legacy Web Audio engine. Microphone pitch tracking remains
deferred until the optional input bus is verified in REAPER and another DAW.
