# Web Play Synth

A browser-based modular virtual-analog synthesizer. The first instrument is a dual-oscillator subtractive voice with a mixer, resonant low-pass filter, dedicated filter and amplifier ADSR envelopes, and a patchable signal-flow view.

## Native Windows VST3 plugin

The native JUCE project builds a Windows x86-64 VST3 plugin and Standalone application from the same source code. Versioned GitHub Releases publish a ZIP containing both.

**[Download the latest Windows VST3 + Standalone package](https://github.com/mintymelinda/web-play-synth/releases/latest/download/web-play-synth-windows-x86_64.zip)**

The link always points directly to the latest published Windows package. The same direct download is available from the **DOWNLOAD WINDOWS VST3** link in the browser synth's header.

To publish a versioned release, merge the changes to `main` and push a version tag:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The tag workflow builds the Windows x86-64 VST3 and Standalone application, runs native audio/MIDI/state tests, verifies the package architecture, validates the VST3 with [Tracktion pluginval](https://github.com/Tracktion/pluginval) at strictness level 5, and attaches the ZIP to a GitHub Release.

Install the `.vst3` bundle using your DAW's plugin installation instructions. The Standalone application is included in the same archive.

## Deploy with Docker

The Nginx container is the permanent front door; application updates replace only versioned static files. Build and publish a release with:

```sh
bash scripts/deploy.sh
```

Open [http://localhost:4173](http://localhost:4173). Set `SYNTH_PORT` to change the host port.

For a Synology deployment, configure the paths once in a local `.env` file in the project directory:

```sh
cp .env.example .env
# Edit .env if your project or data directory differs.
```

Docker Compose reads this file automatically, and the deploy/rollback scripts load it too. It is git-ignored, so your NAS-specific paths stay on the NAS. After that, updates are simply:

```sh
git pull --ff-only origin main
bash scripts/deploy.sh
```

The deploy script builds the app inside Docker (Node.js 22 is not required on the NAS host), stages the output, verifies the generated entry point and referenced assets, and smoke-tests it with Nginx. It then copies fingerprinted assets into a shared directory and atomically switches the `current` release symlink. The existing Nginx container is not restarted for normal application releases.

Old hashed assets are intentionally retained. A browser tab that loaded the previous version can still request its original JavaScript and CSS after an update. The active `index.html` is served with no-cache headers so new visits discover the latest release.

The previous release is retained for rollback:

```sh
bash scripts/rollback.sh
```

Do not delete files from the shared `assets` directory during routine deployments. The release directories and previous-release pointer are stored under `SYNTH_DATA_DIR` (default: `./data`).

## Local development

Requires Node.js 22 or newer.

```sh
npm install
npm run dev
```

To build the native plugin locally, install CMake, a C++17 compiler, and the platform's JUCE build dependencies, then run:

```sh
cmake -S plugin -B build/plugin -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/plugin --config Release --parallel
ctest --test-dir build/plugin -C Release --output-on-failure
```

The CI package verifier can also be run locally against the generated artefacts:

```sh
python scripts/verify_native_package.py --platform linux-x86_64 --root build/plugin/WebPlaySynth_artefacts/Release
```

Use `windows-x86_64` or `macos-universal` for the corresponding platform build.

## First instrument

- Two independently tunable VCOs with sine, triangle, sawtooth, and square waveforms.
- Per-oscillator mixer levels and octave / semitone tuning.
- 24 dB-style resonant low-pass filter controls (cutoff and resonance).
- Dedicated filter and VCA ADSR envelopes.
- Filter contour amount controls how far the filter envelope sweeps the cutoff.
- Interactive keyboard and pointer-playable on-screen keys.
- Modular routing panel showing audio and modulation connections.

The main browser app runs the shared JUCE-independent C++ engine compiled to WebAssembly in an AudioWorklet. The original Web Audio synth remains fully available at `/legacy/`. Browsers require a user gesture before audio can start.


## DAW compatibility

Every Windows native release build runs the VST3 through [Tracktion pluginval](https://github.com/Tracktion/pluginval) at strictness level 5 after the processor MIDI/audio/state smoke tests. This is a compatibility gate, not a substitute for opening the plugin in commercial DAWs. Before declaring a DAW supported, perform and record the manual host matrix in `plugin/README.md`.


## Shared browser engine and legacy synth

The root page uses the same portable C++ voice engine as the native plugin. Its
WebAssembly module is loaded by an AudioWorklet; the worklet reuses preallocated
linear-memory buffers while processing audio blocks. The full original browser
synth (including its original Web Audio engine, UI, keyboard, MIDI CC mapping,
and microphone pitch workflow) remains available at:

- `/legacy/`

The deployment Dockerfile builds both pages and the WebAssembly module. The
browser build requires Emscripten 3.1.74; CI and the deployment image provide it.
