# Web Play Synth

A browser-based modular virtual-analog synthesizer. The first instrument is a dual-oscillator subtractive voice with a mixer, resonant low-pass filter, dedicated filter and amplifier ADSR envelopes, and a patchable signal-flow view.

## Run with Docker

```sh
docker compose up --build
```

Open [http://localhost:4173](http://localhost:4173). Set `SYNTH_PORT` to change the host port.

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
