# Web Play Synth

A browser-based modular virtual-analog synthesizer. The first instrument is a dual-oscillator subtractive voice with a mixer, resonant low-pass filter, dedicated filter and amplifier ADSR envelopes, and a patchable signal-flow view.

## Browser versions

- `/` is the primary browser synthesizer.
- `/legacy/` preserves the original Web Audio implementation as a separately addressable compatibility version. Its existing UI, synthesis engine, and browser-only features remain intact while the shared C++/WebAssembly browser engine is developed and validated.

Both entry points are built and served by the same static deployment. The legacy entry is not a redirect and does not depend on an external service.

## Native VST3 plugin builds

The native JUCE project produces a VST3 plugin and a Standalone application from the same source code. GitHub Actions builds and validates all supported desktop targets:

| Package | Target |
| --- | --- |
| Linux | x86-64 |
| Windows | x86-64 |
| macOS | Universal binary (Intel x86-64 + Apple Silicon arm64) |

Each platform build runs the MIDI/audio/state smoke test and then verifies that both the VST3 bundle and Standalone executable exist and contain binaries for the expected architecture. The macOS check explicitly requires both Intel and Apple Silicon slices.

For pull requests and pushes to `main`, the three platform packages are uploaded as workflow artifacts. Download the artifact matching your operating system from the successful CI run, extract it, and install the `.vst3` bundle using your DAW's plugin installation instructions. The Standalone application is included in the same archive.

To publish a versioned, downloadable release, merge the changes to `main` and push a version tag:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The tag workflow rebuilds and tests all three platforms, then creates a GitHub Release with the Linux, Windows, and universal macOS archives attached. A release is published only if all platform builds, native smoke tests, architecture/package checks, and the browser build pass.

VST3 is a cross-platform plugin standard, but its compiled binaries are operating-system and architecture specific. Use the Windows package on Windows, the macOS package on macOS, and the Linux package on Linux. The macOS package contains both Intel and Apple Silicon architectures.

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

Audio is generated locally using the Web Audio API. Browsers require a user gesture before audio can start.


## DAW compatibility and macOS distribution

Every native CI build runs the VST3 through [Tracktion pluginval](https://github.com/Tracktion/pluginval) at strictness level 5 after the processor MIDI/audio/state smoke test. This exercises host-facing plugin metadata, bus layout, parameter automation/state restoration, lifecycle, and processing behavior. It is a compatibility gate, not a substitute for opening the plugin in commercial DAWs.

The release matrix currently covers the VST3 format on Linux x86-64, Windows x86-64, and universal macOS (Intel + Apple Silicon). Before declaring a DAW supported, perform and record the manual host matrix in `plugin/README.md`; do not infer host compatibility from a successful pluginval run.

### macOS signing and notarization

Tagged releases require a Developer ID Application certificate and App Store Connect API key. Configure these GitHub Actions values:

- Secret `MACOS_CERTIFICATE_P12_BASE64`: base64-encoded PKCS#12 containing the Developer ID Application certificate and private key.
- Secret `MACOS_CERTIFICATE_PASSWORD`: password used to export that PKCS#12 file.
- Secret `APPSTORE_API_PRIVATE_KEY`: contents of the App Store Connect `.p8` API key.
- Variable `APPSTORE_API_KEY_ID`: App Store Connect API key ID.
- Variable `APPSTORE_ISSUER_ID`: App Store Connect issuer ID.

The macOS release job signs the VST3 and Standalone app with the hardened runtime and secure timestamp, verifies their signatures, builds a DMG, signs it, submits it to Apple's notary service, staples the accepted ticket, and verifies Gatekeeper assessment. The unsigned macOS tarball remains a short-retention CI artifact but is excluded from public release assets; the notarized DMG is the public macOS download.

The Developer ID certificate must be issued by Apple for direct distribution (not an Apple Development or Mac App Store distribution certificate). The App Store Connect API key needs permission to submit software for notarization. Keep all certificate and API material in GitHub secrets; never commit it to the repository. If these credentials are not configured, a tagged release intentionally fails rather than publishing an unsigned macOS package.
