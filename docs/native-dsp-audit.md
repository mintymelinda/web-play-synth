# Native DSP correctness, real-time safety, stability, and performance audit

This audit covers four areas in one pass. It is deliberately limited to the native JUCE engine; it does not add synthesis features, presets, or UI work.

## 1. Effect and modulation interactions

The processor applies the effects in this order:
1. Stereo chorus
2. Cross-feedback stereo delay
3. JUCE reverb
4. Master gain

The regression test renders each effect independently, then all three together with LFO filter, amp, pan, and pulse-width destinations active. It checks that each wet control changes the active signal, delay and reverb tails remain audible after note release, live room/damping and delay-time/feedback changes remain finite, and the combined signal stays below the existing peak ceiling of 64.0.

This keeps effect interaction coverage separate from the existing single-route oscillator, filter, envelope, and LFO tests.

## 2. Real-time callback safety

The callback path was reviewed from processBlock through parameter snapshotting, MIDI dispatch, voice rendering, and effects:
- Chorus and delay storage is allocated/resized in prepareToPlay, not in processBlock.
- Voice/filter state is prepared before rendering.
- The render path uses fixed-size voice/layer arrays and preallocated effect buffers.
- No application-level file I/O, waits, mutex acquisition, or container growth is performed by the audio callback.
- The callback's MIDI CC mapping path uses JUCE's setValueNotifyingHost so the host is informed of MIDI-driven parameter changes. This is the only host-notification boundary in the MIDI control path; it must remain covered by host smoke tests.
- A dedicated test instruments ordinary heap new/new[] calls after warm-up and exercises full polyphony, all effects, and an already-learned MIDI CC mapping over 128 callback blocks.

The allocation assertion is specifically for steady-state callback execution; setup, parameter configuration, and buffer preparation are intentionally outside the measured interval.

## 3. Numerical stability and parameter boundaries

The callback already scopes juce::ScopedNoDenormals around the complete processing path. The audit test now:
- Sets every exposed parameter to its minimum, processes a note, then sets every parameter to its maximum.
- Renders the maximum state at 96 kHz through long effect tails.
- Checks every output sample for NaN/Inf.
- Preserves the established absolute peak limit of 64.0; it does not add a limiter or relax the existing stress threshold.
- Counts subnormal output samples and requires zero.

The existing DSP stress matrix continues to exercise 16 simultaneous notes, four-layer unison, hard sync, cross modulation, maximum filter drive/resonance, modulation, and all effects at 44.1, 48, and 96 kHz.

## 4. Polyphony/effects performance

A repeatable native benchmark warms the processor, then measures 256 blocks at 48 kHz / 256 samples with:
- 16 simultaneous voices
- Four unison layers per voice
- Hard sync and cross modulation
- Maximum LFO pitch/filter/amp/pan/PWM modulation
- Chorus, delay, and reverb enabled at their upper operating limits

It reports the elapsed processing time as a multiple of the corresponding real-time audio duration and fails if the measured workload cannot render faster than real time. The timing is an aggregate over the measured blocks to reduce sensitivity to a single scheduler interruption.

## Validation

The native-dsp-audit CTest target contains the four focused checks above. Completion is contingent on the Windows, Linux, and macOS native CI matrix passing, alongside the existing native regression suite, VST3 host-audio smoke test, plugin validation, and browser tests. No platform or DAW is claimed validated beyond the automated jobs that actually run.