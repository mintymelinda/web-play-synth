# Microphone pitch input — feasibility and architecture

**Status:** Proposed architecture; implementation is gated on host-routing validation.  
**Scope:** Optional monophonic pitch tracking for the JUCE VST3/Standalone instrument.  
**Non-goal:** Polyphonic audio-to-MIDI, microphone device management, or audio-through effects processing.

## Decision

Use a **host-routed optional mono audio input bus** on the instrument. The DAW owns device selection, microphone permissions, input monitoring, and routing. The plugin analyses the incoming signal to control a dedicated monophonic synth voice; it does not monitor, mix, or pass the source audio through to its output.

This is preferable to opening an audio device from inside the plugin. A plugin must not independently request microphone permission or compete with the host for exclusive device access. It also keeps the pitch detector independent of browser APIs and suitable for eventual reuse in the shared C++ engine.

This is an architecture recommendation, not a claim that every DAW supports audio routing into an instrument. That is the principal feasibility gate.

## Findings from the current implementation

- The JUCE processor currently declares MIDI input and mono/stereo output, but no audio input bus.
- The processor is a VST3 instrument (IS_SYNTH TRUE, NEEDS_MIDI_INPUT TRUE).
- Its existing 16-voice pool is driven by MIDI note events. Microphone pitch is monophonic and should not consume or steal those voices.
- The browser version already has a monophonic pitch-following path, a 55–1000 Hz accepted frequency range, a level gate, and a separate tracked voice. It requests microphone access in the browser; that device/permission code must not be ported to the plugin.
- The existing JUCE processing path clears the output buffer and renders synth voices. The new input must be analysis-only so the source signal cannot accidentally be heard or create an audio feedback loop.

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

The pitch-analysis worker must never call into the synth voice pool, mutate APVTS parameters, or access editor components. It publishes only a bounded, validated pitch estimate and gate state. The audio thread consumes those values and owns all voice changes.

## Input bus and host contract

- Add one optional mono input bus named **Pitch Input**, disabled by default.
- Preserve the existing mono/stereo output layouts and MIDI input.
- Accept the input bus only when it is disabled or mono. Do not silently reinterpret multichannel input as a microphone array; downmix only if a host supplies a stereo layout that is deliberately supported.
- Do not copy input samples to output. The plugin remains an instrument, not an audio-through effect.
- Do not enumerate devices, open AudioDeviceManager, request permissions, or access platform microphone APIs from plugin code.
- The user enables the bus and routes a mono audio track/input to it in the DAW. The editor should show a clear “no pitch input routed” / “tracking” state rather than pretending to know which physical microphone is selected.
- Keep the bus disabled by default so existing sessions and hosts retain their current behavior.
- Use JUCE's bus-aware buffer access (getBusBuffer / getChannelIndexInProcessBlockBuffer) rather than assuming input and output channels occupy the same offsets.
- JUCE documents that processBlock receives at least the maximum of total input/output channels. Input-only channels may be read-only when there are more inputs than outputs. The current unconditional whole-buffer clear must therefore be replaced with bus-aware output clearing: inspect/copy the input first, then clear/write only the output bus. Never clear or write input-only channels.
- JUCE references: [AudioProcessor::processBlock and getBusBuffer](https://docs.juce.com/develop/classjuce_1_1AudioProcessor.html), [JUCE audio bus layout tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/).

**Important compatibility risk:** Some DAWs do not expose audio routing into instrument plugins, or expose it differently for instrument/sidechain buses. pluginval can validate plugin structure and processing but cannot prove the routing UI exists or works in a real DAW. Do not mark the feature generally supported until the host matrix below is exercised.

## Pitch analysis and timing

Use a monophonic detector (YIN/CMND or normalized autocorrelation with confidence checks), not polyphonic transcription. The browser detector is a behavioral reference, not a requirement to copy its current quadratic work into the real-time callback.

- Capture samples into a preallocated single-producer/single-consumer FIFO from processBlock.
- Run window analysis on a dedicated worker thread. No heap allocation, blocking locks, logging, UI calls, or unbounded work in the audio callback.
- Publish the latest estimate through atomics; apply pitch/gate changes to the dedicated voice only on the audio thread.
- Start evaluation with a 55–1000 Hz range to match the browser feature. Reject low-confidence estimates and smooth/hysteresis-filter pitch changes to reduce octave jumps and vibrato chatter.
- Use an RMS/noise gate with attack/release hysteresis. Silence or loss of confidence must release the mic voice; it must never leave a stuck note.
- Reuse the synth's oscillator, filter, envelope, glide, and master controls. The microphone voice is monophonic and separate from the 16 MIDI voices.
- Analysis windows must balance low-note stability and latency. At 48 kHz, 2048 samples span 42.7 ms and 4096 span 85.3 ms. A 55 Hz signal has a period of about 18.2 ms, so the lowest octave may require the longer window. These are window durations, not end-to-end latency promises; hop size, scheduling, host block size, and smoothing add delay.
- Measure CPU and pitch-to-sound latency at 44.1, 48, 96, and 192 kHz and common block sizes before selecting defaults. If the low-note latency is unacceptable, narrow the supported low-frequency range rather than hiding the delay with aggressive tracking.

## Voice interaction rules

- MIDI note input continues to use the existing 16-voice pool and must remain unaffected when pitch tracking is disabled.
- The microphone gets one dedicated monophonic voice, so it cannot steal a MIDI voice.
- The mic voice uses the same patch parameters and synth output as MIDI voices.
- MIDI and microphone voices may sound together. This is deliberate and should be documented; it avoids surprising priority rules and lets the player blend a mic-controlled line with keyboard performance.
- A tracking gate close, disabled input, bus disconnection, transport stop, or plugin release must release/clear the mic voice.
- Do not create synthetic MIDI events or feed pitch estimates back through the MIDI CC mapping path.

## UX proposal

Keep the feature optional and visibly separate from MIDI:
- A **Pitch Tracking** enable control, off by default and saved with plugin state.
- A status line with states such as **Input not routed**, **Listening for pitch**, **Tracking C4 · +7 cents**, and **No stable pitch**.
- A simple input level/gate indication is useful; avoid adding physical device selection or a second routing panel.
- Explain that the user must route a mono track/input to the plugin's Pitch Input bus in the DAW.
- If the host does not expose the input bus, show a useful unavailable state; do not fall back to opening the microphone directly.

The status display is informational and must not drive audio processing. Any UI polling reads atomics only.

## Feasibility gates and implementation sequence

### Gate 1 — prove host routing before implementing the detector

1. Add the optional mono input bus without changing default output or MIDI behavior.
2. Confirm JUCE exposes the expected VST3 bus arrangement and the plugin still passes strictness-level-5 pluginval.
3. Exercise real routing in at least REAPER and one other DAW (prefer Bitwig or Ableton): route a mono audio track into the instrument's Pitch Input bus and confirm the processor receives non-zero samples.
4. Verify the bus can be disabled and that old sessions/default layouts remain valid.
5. If common hosts cannot route audio into an instrument, stop and evaluate a separate companion audio-to-MIDI effect or a separate instrument/effect variant. Do not compromise the existing instrument's host compatibility to force this feature.

### Gate 2 — validate the DSP in isolation

- Unit-test known sine waves at 55, 65, 82, 110, 220, 440, 880, and 1000 Hz.
- Test silence, low-level signals, broadband noise, harmonics, vibrato, fast note changes, and abrupt input disconnection.
- Measure pitch error, octave-error rate, gate response, end-to-end latency, and CPU.
- Confirm all analysis buffers and worker communication are bounded and preallocated.

### Gate 3 — integrate the monophonic voice

- Add a dedicated mic voice independent of the MIDI voice array.
- Test MIDI-only, mic-only, and simultaneous MIDI + mic playback.
- Verify note release on silence, bus disconnect, disable, stop, and releaseResources.
- Verify no source-audio pass-through and no change to existing MIDI/CC/state behavior.
- Persist the enable setting; do not persist transient pitch/gate state.

### Gate 4 — editor and host acceptance

- Add the minimal enable/status UI only after the input bus is proven in hosts.
- Record DAW name/version, OS, bus-routing steps, and observed latency.
- Keep microphone pitch tracking optional and document host-specific limitations.

## Acceptance criteria

- Default plugin behavior and saved sessions remain compatible with the input bus disabled.
- The host can route mono audio into the plugin in each host declared supported.
- Input samples are never passed through to the plugin output.
- Stable monophonic pitch tracking works across the tested range with documented error/latency.
- No stuck notes on silence, disconnection, stop, disable, or release.
- MIDI polyphony, MIDI CC mapping, host automation, and state restore remain intact.
- No microphone/device permission or device-selection code exists in the plugin.
- CPU, latency, and host-routing results are recorded rather than assumed.

## Current conclusion

**Technically plausible, but host routing is unproven.** The optional host-routed mono input bus is the preferred design because it respects DAW ownership and keeps the detector portable. The next implementation should be a narrow bus-routing spike plus host validation, not a full pitch detector or UI. If the DAW matrix fails, evaluate a companion audio-to-MIDI plugin before proceeding.
