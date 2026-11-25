# Microphone pitch input — feasibility and implementation gates

**Status:** Input-bus feasibility spike implemented; real-host routing validation still required. Pitch detection and the dedicated synth voice remain deferred.
**Scope:** Optional monophonic pitch tracking for the JUCE VST3/Standalone instrument.
**Non-goals:** Polyphonic audio-to-MIDI, plugin-owned microphone/device management, or microphone audio pass-through.

## Decision

Use a host-routed optional mono audio input bus. The DAW owns device selection, microphone permissions, input monitoring, and routing. The plugin will analyse incoming samples and control a dedicated monophonic synth voice; it will not monitor, mix, or pass source audio through to its output.

Do not open an audio device from inside the plugin. Do not add platform microphone APIs or browser permission flows to the native plugin.

## Current implementation (main)

The first feasibility step is already present:

- The processor declares an optional mono input bus named **Pitch Input**, disabled by default.
- The processor accepts the disabled or mono input layout while retaining supported mono/stereo output layouts and MIDI input.
- The audio callback reads the input through JUCE's bus-aware buffer API before touching the output bus.
- It publishes an input RMS value for the editor meter. It does not yet detect pitch or generate notes.
- The editor indicates whether the bus is enabled and displays its measured input level.
- Native smoke tests cover the default disabled layout, enabling the mono bus, delivery of a known input signal, continued MIDI sound while input is present, and isolation of input audio from the synth output.

These tests prove processor-level buffer handling. They do **not** prove that a DAW exposes a usable routing path to this VST3 input bus.

The old browser microphone workflow remains available only through `/legacy/`. The root browser app now uses the shared C++ engine compiled to WebAssembly; do not silently reintroduce the legacy Web Audio engine into the root app.

## Proposed signal flow

```text
Microphone / instrument / audio track
             |
       DAW input routing
             |
   Optional mono plugin input bus
             |
    downmix + bounded sample FIFO
             |
     pitch-analysis worker thread
             |
   frequency + confidence + gate atomics
             |
       audio processing thread
             |
  dedicated monophonic synth voice
             |
     existing synth output bus

MIDI input ----------------> existing 16-voice pool
```

The worker must never call the synth engine, mutate APVTS parameters, or access editor components. It publishes only bounded, validated pitch/gate results. The audio thread owns all voice changes. The microphone voice must not consume or steal any of the existing 16 MIDI voices.

## Gate 1 — real-host routing (still open)

Before implementing pitch detection or adding tracking UI:

1. Load the current VST3 in REAPER and at least one other DAW (prefer Bitwig or Ableton).
2. Enable the plugin's **Pitch Input** bus in the host's plugin I/O or routing controls.
3. Route a mono audio track/input to that bus.
4. Confirm the plugin's input meter responds to the routed signal.
5. Confirm MIDI notes continue to sound while the input bus is enabled.
6. Disable the bus and reopen a session saved with the default MIDI-only layout; verify the old layout remains usable.
7. Record DAW name/version, OS, routing steps, and any host-specific limitations.

A passing pluginval run or a direct `AudioProcessor::processBlock` smoke test is not evidence that the DAW exposes this routing UI. Do not mark a host supported without the manual check.

If common hosts cannot route audio into an instrument, stop and evaluate a companion audio-to-MIDI effect or a separate instrument/effect variant. Do not compromise the existing instrument's host compatibility to force this feature.

## Gate 2 — detector in isolation

After Gate 1 passes, implement a monophonic detector (YIN/CMND or normalized autocorrelation with confidence checks). The browser detector is a behavioral reference, not a requirement to copy its current quadratic work into the real-time callback.

- Capture samples into a preallocated single-producer/single-consumer FIFO from `processBlock`.
- Run window analysis on a dedicated worker thread. No heap allocation, blocking locks, logging, UI calls, or unbounded work in the audio callback.
- Publish the latest estimate through atomics; apply pitch/gate changes only on the audio thread.
- Start with the browser's 55–1000 Hz range. Reject low-confidence estimates and use smoothing/hysteresis to reduce octave jumps and vibrato chatter.
- Use an RMS/noise gate with attack/release hysteresis. Silence or loss of confidence must release the mic voice.
- Test known tones at 55, 65, 82, 110, 220, 440, 880, and 1000 Hz, plus silence, low-level signals, noise, harmonics, vibrato, fast note changes, and abrupt disconnection.
- Measure pitch error, octave-error rate, gate response, CPU, and pitch-to-sound latency at 44.1, 48, 96, and 192 kHz and common block sizes.

At 48 kHz, 2048 samples span 42.7 ms and 4096 samples span 85.3 ms. A 55 Hz signal has a period of about 18.2 ms, so low-note stability may require a longer window. These are window durations, not end-to-end latency promises; hop size, scheduling, host block size, and smoothing add delay. Narrow the supported low-frequency range if measured latency is unacceptable rather than hiding it with aggressive tracking.

## Gate 3 — dedicated voice integration

- Add one monophonic mic voice independent of the MIDI voice pool.
- Reuse the authoritative synth engine's oscillator, filter, envelope, glide, and master controls.
- Keep MIDI and microphone voices able to sound together; do not add synthetic MIDI events or route pitch estimates through MIDI CC mapping.
- Release the mic voice on silence, loss of confidence, bus disconnect, disable, transport stop, and plugin resource release.
- Verify MIDI-only, mic-only, and simultaneous playback, plus no source-audio pass-through.
- Persist only the enable setting; never persist transient pitch/gate state.

## Gate 4 — minimal editor and acceptance

Only after routing and detector behavior are proven, add an enable control (off by default) and a concise status display such as **Input not routed**, **Listening for pitch**, **Tracking C4 · +7 cents**, or **No stable pitch**. UI polling may read atomics only and must not drive processing.

Record host/version, OS, routing steps, latency, CPU, and known limitations. The DAW remains responsible for device selection and permissions.

## Acceptance criteria

- Existing MIDI-only sessions and default behavior remain compatible with the input bus disabled.
- Each declared supported host can route mono audio into the plugin.
- Input audio is never passed through to the output.
- Pitch tracking is stable across the measured supported range, with documented error and latency.
- Silence, disconnection, stop, disable, and release cannot leave a stuck mic voice.
- MIDI polyphony, MIDI CC mapping, automation, and state restoration remain intact.
- No plugin-owned microphone permission or device-selection code exists.
- Real-host routing, CPU, and latency claims are backed by recorded evidence.

## Current conclusion

**The processor-level input-bus spike is complete; real-host routing is the next task.** The input RMS meter and smoke tests establish that the current processor can safely receive and isolate mono input. They do not establish DAW routing compatibility. Keep pitch detection, dedicated voice integration, and editor controls deferred until Gate 1 is demonstrated in real hosts.
