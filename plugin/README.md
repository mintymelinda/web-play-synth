# Web Play Synth — Native Plugin

This directory contains the native C++ instrument built with JUCE. It is separate from the React/Web Audio application, which remains independently deployable.

## Formats

- VST3
- Standalone application

Audio Unit can be added to the same project for macOS. The first native milestone establishes VST3 and Standalone builds on Linux.

## Build

Requirements: CMake 3.22+, a C++17 compiler, Git, and the platform audio/GUI development packages required by JUCE.

Configure with: `cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`

Build with: `cmake --build build/plugin --config Release --parallel`

Run native smoke tests with: `ctest --test-dir build/plugin -C Release --output-on-failure`

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

The native editor provides grouped oscillator, filter, envelope, LFO, output, glide, and routing controls. Parameters remain host-automatable and use JUCE attachments so host automation and the editor stay synchronized.

## CI artifacts

The Linux CI job builds both VST3 and Standalone formats, runs a MIDI/audio/state smoke test, and publishes a downloadable `web-play-synth-linux-x86_64` artifact for 14 days.

## Experimental pitch input bus

The processor exposes an optional mono input bus named **Pitch Input**. It is disabled by default, so existing MIDI-only sessions retain their original input/output configuration. A host can enable and route a mono audio source to this bus while the instrument continues to render MIDI normally.

This feasibility spike reports the routed input level internally for automated smoke testing; it does not yet perform pitch detection or generate notes from the input. Input audio is never passed through to the synth output. Real routing support still needs to be verified in REAPER and Bitwig or Ableton before the pitch-tracking implementation proceeds.

## Licensing

JUCE is dual-licensed. Before distributing binaries, choose and comply with the JUCE licence that applies (including its AGPLv3 or commercial terms). The plugin's distribution terms must be compatible with that choice.


## DAW compatibility

CI runs Tracktion pluginval against the built VST3 at strictness level 5 on each supported OS. This adds host-like checks for plugin discovery/metadata, buses, parameter automation, state restoration, lifecycle, and processing. The existing native smoke test separately verifies MIDI note-on audio, state save/restore, note-off, and release behavior.

Pluginval is a strong automated compatibility signal, not proof that every DAW works. Use this manual acceptance matrix before marking a host as supported:

| Host | Linux x86-64 | Windows x86-64 | macOS Intel | macOS Apple Silicon |
| --- | --- | --- | --- | --- |
| REAPER | Not yet verified | Not yet verified | Not yet verified | Not yet verified |
| Ardour | Not yet verified | N/A | Not yet verified | Not yet verified |
| Bitwig Studio | Not yet verified | Not yet verified | Not yet verified | Not yet verified |
| Ableton Live | N/A | Not yet verified | Not yet verified | Not yet verified |
| FL Studio | N/A | Not yet verified | N/A | Not yet verified |
| Logic Pro (AU required) | N/A | N/A | Not yet verified | Not yet verified |

For each host, verify plugin scan/discovery, stereo output, MIDI note input, automation read/write, preset or project save/reopen, editor open/close, transport stop/restart, and repeated load/unload. Record the exact host version and OS build with each result. Logic Pro is listed as a future Audio Unit target; this project currently builds VST3 and Standalone only, so it is not yet supported by this artifact.

## macOS signing and notarization

The release workflow uses a Developer ID Application identity to sign the VST3 bundle and Standalone app with hardened runtime and secure timestamps. It then creates a signed DMG, submits it to Apple's notary service, staples the accepted ticket, and checks the stapled ticket and Gatekeeper assessment.

Required GitHub configuration:

- Secret `MACOS_CERTIFICATE_P12_BASE64`: base64-encoded Developer ID Application certificate and private key in PKCS#12 format.
- Secret `MACOS_CERTIFICATE_PASSWORD`: PKCS#12 export password.
- Secret `APPSTORE_API_PRIVATE_KEY`: App Store Connect API `.p8` private key.
- Variables `APPSTORE_API_KEY_ID` and `APPSTORE_ISSUER_ID`.

Use an Apple-issued Developer ID Application certificate, not an Apple Development or Mac App Store certificate. The API key must have notarization permissions. Until these values are configured, PR builds still run normally, but tagged release publishing will fail closed rather than ship an unsigned macOS package.
