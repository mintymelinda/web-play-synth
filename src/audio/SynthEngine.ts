export type Waveform = OscillatorType;

export interface Envelope {
  attack: number;
  decay: number;
  sustain: number;
  release: number;
}

export interface SynthSettings {
  oscillator1: { waveform: Waveform; octave: number; tune: number; level: number };
  oscillator2: { waveform: Waveform; octave: number; tune: number; level: number; hardSync: boolean; crossMod: number };
  filter: { cutoff: number; resonance: number; contour: number };
  lfo: { waveform: Waveform; rate: number; pitchDepth: number; filterDepth: number };
  filterEnvelope: Envelope;
  ampEnvelope: Envelope;
  master: number;
  glide: { enabled: boolean; time: number };
  routing: { vco1: boolean; vco2: boolean; filterEnv: boolean; ampEnv: boolean };
}

interface Voice {
  oscillators: [OscillatorNode, OscillatorNode];
  oscillatorGains: [GainNode, GainNode];
  lfo: OscillatorNode;
  lfoPitchGain: GainNode;
  lfoFilterGain: GainNode;
  crossModGain: GainNode;
  filter: BiquadFilterNode;
  amp: GainNode;
  voiceGain: GainNode;
  note: number;
  pitchFrequency: number | null;
  released: boolean;
  connected: [boolean, boolean];
}

const DEFAULT_SETTINGS: SynthSettings = {
  oscillator1: { waveform: "sawtooth", octave: 0, tune: 0, level: 0.72 },
  oscillator2: { waveform: "square", octave: 0, tune: 7, level: 0.38, hardSync: false, crossMod: 0 },
  filter: { cutoff: 1800, resonance: 0.8, contour: 0.55 },
  lfo: { waveform: "sine", rate: 4.5, pitchDepth: 0, filterDepth: 0 },
  filterEnvelope: { attack: 0.025, decay: 0.42, sustain: 0.32, release: 0.38 },
  ampEnvelope: { attack: 0.015, decay: 0.25, sustain: 0.78, release: 0.32 },
  master: 0.72,
  glide: { enabled: false, time: 0.18 },
  routing: { vco1: true, vco2: true, filterEnv: true, ampEnv: true },
};

const clamp = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

function createHardSyncWave(context: AudioContext, ratio: number) {
  const sampleCount = 2048;
  const harmonicCount = 96;
  const real = new Float32Array(harmonicCount + 1);
  const imag = new Float32Array(harmonicCount + 1);
  const samples = new Float32Array(sampleCount);

  for (let sample = 0; sample < sampleCount; sample += 1) {
    const phase = sample / sampleCount;
    samples[sample] = 2 * ((phase * ratio) % 1) - 1;
  }

  for (let harmonic = 1; harmonic <= harmonicCount; harmonic += 1) {
    let cosine = 0;
    let sine = 0;
    for (let sample = 0; sample < sampleCount; sample += 1) {
      const angle = (2 * Math.PI * harmonic * sample) / sampleCount;
      cosine += samples[sample] * Math.cos(angle);
      sine += samples[sample] * Math.sin(angle);
    }
    real[harmonic] = (2 * cosine) / sampleCount;
    imag[harmonic] = (2 * sine) / sampleCount;
  }

  return context.createPeriodicWave(real, imag, { disableNormalization: false });
}

export class SynthEngine {
  private context: AudioContext | null = null;
  private master: GainNode | null = null;
  private voices = new Map<number, Voice>();
  private syncWaves = new Map<number, PeriodicWave>();
  private settings: SynthSettings = structuredClone(DEFAULT_SETTINGS);
  private lastPlayedFrequency: number | null = null;

  get isReady() {
    return this.context !== null && this.context.state === "running";
  }

  async start() {
    if (!this.context) {
      this.context = new AudioContext();
      this.master = this.context.createGain();
      this.master.gain.value = this.settings.master;
      this.master.connect(this.context.destination);
    }
    if (this.context.state !== "running") await this.context.resume();
  }

  update(settings: SynthSettings) {
    this.settings = settings;
    if (this.master && this.context) {
      this.master.gain.setTargetAtTime(settings.master, this.context.currentTime, 0.015);
    }
    for (const voice of this.voices.values()) this.updateVoice(voice);
  }

  noteOn(note: number, velocity = 0.82, pitchFrequency: number | null = null) {
    if (!this.context || !this.master || this.voices.has(note)) return;
    const context = this.context;
    const now = context.currentTime;
    const voiceGain = context.createGain();
    const filter = context.createBiquadFilter();
    const amp = context.createGain();
    const oscillator1 = context.createOscillator();
    const oscillator2 = context.createOscillator();
    const gain1 = context.createGain();
    const gain2 = context.createGain();
    const lfo = context.createOscillator();
    const lfoPitchGain = context.createGain();
    const lfoFilterGain = context.createGain();
    const crossModGain = context.createGain();

    filter.type = "lowpass";
    filter.Q.value = this.settings.filter.resonance;
    amp.gain.setValueAtTime(0, now);
    voiceGain.gain.value = 1;

    oscillator1.connect(gain1);
    oscillator2.connect(gain2);
    oscillator2.connect(crossModGain);
    crossModGain.connect(oscillator1.frequency);
    lfo.connect(lfoPitchGain);
    lfoPitchGain.connect(oscillator1.frequency);
    lfoPitchGain.connect(oscillator2.frequency);
    lfo.connect(lfoFilterGain);
    lfoFilterGain.connect(filter.frequency);

    voiceGain.connect(filter);
    filter.connect(amp);
    amp.connect(this.master);

    const voice: Voice = {
      oscillators: [oscillator1, oscillator2],
      oscillatorGains: [gain1, gain2],
      lfo,
      lfoPitchGain,
      lfoFilterGain,
      crossModGain,
      filter,
      amp,
      voiceGain,
      note,
      pitchFrequency,
      released: false,
      connected: [false, false],
    };
    this.voices.set(note, voice);
    this.updateVoice(voice);

    // Portamento applies to keyboard/MIDI note changes, not the continuously tracked mic pitch.
    if (pitchFrequency === null) {
      const targetFrequency = 440 * 2 ** ((note - 69) / 12);
      const glideFrom = this.lastPlayedFrequency;
      this.lastPlayedFrequency = targetFrequency;
      if (this.settings.glide.enabled && glideFrom !== null && this.settings.glide.time > 0) {
        const glideEnd = now + this.settings.glide.time;
        const offsets = [
          2 ** ((this.settings.oscillator1.octave * 12 + this.settings.oscillator1.tune) / 12),
          2 ** ((this.settings.oscillator2.octave * 12 + this.settings.oscillator2.tune) / 12),
        ];
        voice.oscillators.forEach((oscillator, index) => {
          const target = targetFrequency * offsets[index];
          const start = glideFrom * offsets[index];
          oscillator.frequency.cancelScheduledValues(now);
          oscillator.frequency.setValueAtTime(Math.max(0.01, start), now);
          oscillator.frequency.exponentialRampToValueAtTime(Math.max(0.01, target), glideEnd);
        });
      }
    }

    const ampEnv = this.settings.ampEnvelope;
    const filterEnv = this.settings.filterEnvelope;
    if (this.settings.routing.ampEnv) {
      amp.gain.setValueAtTime(0, now);
      amp.gain.linearRampToValueAtTime(velocity, now + ampEnv.attack);
      amp.gain.linearRampToValueAtTime(velocity * ampEnv.sustain, now + ampEnv.attack + ampEnv.decay);
    } else {
      amp.gain.setValueAtTime(velocity, now);
    }

    filter.frequency.setValueAtTime(clamp(this.settings.filter.cutoff, 30, 18000), now);
    if (this.settings.routing.filterEnv) {
      const peak = this.settings.filter.cutoff * 2 ** (this.settings.filter.contour * 3);
      filter.frequency.exponentialRampToValueAtTime(clamp(peak, 31, 19000), now + Math.max(0.005, filterEnv.attack));
      filter.frequency.exponentialRampToValueAtTime(
        clamp(this.settings.filter.cutoff * 2 ** (this.settings.filter.contour * (filterEnv.sustain * 3)), 31, 19000),
        now + filterEnv.attack + filterEnv.decay,
      );
    }

    for (const oscillator of voice.oscillators) oscillator.start(now);
    lfo.start(now);
  }

  setVoicePitch(note: number, frequency: number) {
    const voice = this.voices.get(note);
    if (!voice || voice.released || !Number.isFinite(frequency) || frequency <= 0) return;
    voice.pitchFrequency = clamp(frequency, 20, 20000);
    this.updateVoice(voice);
  }

  setVoiceGate(note: number, open: boolean, velocity = 0.82) {
    const voice = this.voices.get(note);
    if (!voice || voice.released || !this.context) return;
    const now = this.context.currentTime;
    voice.amp.gain.cancelScheduledValues(now);
    voice.amp.gain.setValueAtTime(voice.amp.gain.value, now);
    if (open && this.settings.routing.ampEnv) {
      const envelope = this.settings.ampEnvelope;
      voice.amp.gain.linearRampToValueAtTime(velocity, now + envelope.attack);
      voice.amp.gain.linearRampToValueAtTime(velocity * envelope.sustain, now + envelope.attack + envelope.decay);
    } else {
      const release = this.settings.routing.ampEnv ? this.settings.ampEnvelope.release : 0.018;
      voice.amp.gain.linearRampToValueAtTime(open ? velocity : 0, now + (open ? 0.025 : release));
    }
  }

  noteOff(note: number) {
    const voice = this.voices.get(note);
    if (!voice || !this.context || voice.released) return;
    voice.released = true;
    const now = this.context.currentTime;
    const amp = this.settings.ampEnvelope;
    const filter = this.settings.filterEnvelope;

    voice.amp.gain.cancelScheduledValues(now);
    voice.amp.gain.setValueAtTime(voice.amp.gain.value, now);
    voice.amp.gain.linearRampToValueAtTime(0, now + (this.settings.routing.ampEnv ? amp.release : 0.02));
    voice.filter.frequency.cancelScheduledValues(now);
    voice.filter.frequency.setTargetAtTime(
      clamp(this.settings.filter.cutoff, 31, 19000),
      now,
      Math.max(0.01, filter.release / 5),
    );

    const stopAt = now + Math.max(amp.release, filter.release) + 0.08;
    for (const oscillator of voice.oscillators) oscillator.stop(stopAt);
    voice.lfo.stop(stopAt);
    window.setTimeout(() => {
      voice.oscillators.forEach((oscillator) => oscillator.disconnect());
      voice.oscillatorGains.forEach((gain) => gain.disconnect());
      voice.lfo.disconnect();
      voice.lfoPitchGain.disconnect();
      voice.lfoFilterGain.disconnect();
      voice.crossModGain.disconnect();
      voice.voiceGain.disconnect();
      voice.filter.disconnect();
      voice.amp.disconnect();
      this.voices.delete(note);
    }, (stopAt - now) * 1000 + 30);
  }

  allNotesOff() {
    for (const note of this.voices.keys()) this.noteOff(note);
  }

  private getHardSyncWave(ratio: number) {
    if (!this.context) throw new Error("Audio context is unavailable");
    const key = Math.round(clamp(ratio, 0.25, 8) * 1000) / 1000;
    let wave = this.syncWaves.get(key);
    if (!wave) {
      wave = createHardSyncWave(this.context, key);
      this.syncWaves.set(key, wave);
    }
    return wave;
  }

  private updateVoice(voice: Voice) {
    if (!this.context) return;
    const now = this.context.currentTime;
    const { oscillator1, oscillator2, lfo, filter } = this.settings;
    const noteFrequency = voice.pitchFrequency ?? 440 * 2 ** ((voice.note - 69) / 12);
    const pitchSmoothing = this.settings.glide.enabled ? Math.max(0.005, this.settings.glide.time / 3) : 0.015;
    const baseFrequency1 = noteFrequency * 2 ** ((oscillator1.octave * 12 + oscillator1.tune) / 12);
    const baseFrequency2 = noteFrequency * 2 ** ((oscillator2.octave * 12 + oscillator2.tune) / 12);
    const syncRatio = baseFrequency2 / baseFrequency1;

    voice.oscillators[0].type = oscillator1.waveform;
    voice.oscillators[0].frequency.setTargetAtTime(baseFrequency1, now, pitchSmoothing);
    if (oscillator2.hardSync) {
      voice.oscillators[1].setPeriodicWave(this.getHardSyncWave(syncRatio));
      voice.oscillators[1].frequency.setTargetAtTime(baseFrequency1, now, 0.015);
    } else {
      voice.oscillators[1].type = oscillator2.waveform;
      voice.oscillators[1].frequency.setTargetAtTime(baseFrequency2, now, pitchSmoothing);
    }

    const configs = [oscillator1, oscillator2];
    configs.forEach((config, index) => {
      const routeEnabled = index === 0 ? this.settings.routing.vco1 : this.settings.routing.vco2;
      if (routeEnabled && !voice.connected[index]) {
        voice.oscillatorGains[index].connect(voice.voiceGain);
        voice.connected[index] = true;
      } else if (!routeEnabled && voice.connected[index]) {
        voice.oscillatorGains[index].disconnect(voice.voiceGain);
        voice.connected[index] = false;
      }
      voice.oscillatorGains[index].gain.setTargetAtTime(config.level, now, 0.015);
    });

    voice.crossModGain.gain.setTargetAtTime(oscillator2.crossMod, now, 0.015);
    voice.lfo.type = lfo.waveform;
    voice.lfo.frequency.setTargetAtTime(lfo.rate, now, 0.015);
    voice.lfoPitchGain.gain.setTargetAtTime(lfo.pitchDepth, now, 0.015);
    voice.lfoFilterGain.gain.setTargetAtTime(lfo.filterDepth, now, 0.015);
    voice.filter.Q.setTargetAtTime(filter.resonance, now, 0.015);
    if (!this.settings.routing.filterEnv) voice.filter.frequency.setTargetAtTime(filter.cutoff, now, 0.015);
  }
}
