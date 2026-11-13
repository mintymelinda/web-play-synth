# Web Play Synth

A browser-based modular virtual-analog synthesizer. The first instrument is a dual-oscillator subtractive voice with a mixer, resonant low-pass filter, dedicated filter and amplifier ADSR envelopes, and a patchable signal-flow view.

## Deploy with Docker

The Nginx container is the permanent front door; application updates replace only versioned static files. Build and publish a release with:

```sh
bash scripts/deploy.sh
```

Open [http://localhost:4173](http://localhost:4173). Set `SYNTH_PORT` to change the host port.

For a Synology deployment, keep release data outside the repository and pass its persistent path:

```sh
SYNTH_DATA_DIR=/volume1/docker/web-play-synth/data SYNTH_PORT=4173 bash scripts/deploy.sh
```

The deploy script builds the app inside Docker (Node.js 22 is not required on the NAS host), stages the output, verifies the generated entry point and referenced assets, and smoke-tests it with Nginx. It then copies fingerprinted assets into a shared directory and atomically switches the `current` release symlink. The existing Nginx container is not restarted for normal application releases.

Old hashed assets are intentionally retained. A browser tab that loaded the previous version can still request its original JavaScript and CSS after an update. The active `index.html` is served with no-cache headers so new visits discover the latest release.

The previous release is retained for rollback:

```sh
SYNTH_DATA_DIR=/volume1/docker/web-play-synth/data bash scripts/rollback.sh
```

Do not delete files from the shared `assets` directory during routine deployments. The release directories and previous-release pointer are stored under `SYNTH_DATA_DIR` (default: `./data`).

## Local development

Requires Node.js 22 or newer.

```sh
npm install
npm run dev
```

## First instrument

- Two independently tunable VCOs with sine, triangle, sawtooth, and square waveforms.
- Per-oscillator mixer levels and octave / semitone tuning.
- 24 dB-style resonant low-pass filter controls (cutoff and resonance).
- Dedicated filter and VCA ADSR envelopes.
- Filter contour amount controls how far the filter envelope sweeps the cutoff.
- Interactive keyboard and pointer-playable on-screen keys.
- Modular routing panel showing audio and modulation connections.

Audio is generated locally using the Web Audio API. Browsers require a user gesture before audio can start.
