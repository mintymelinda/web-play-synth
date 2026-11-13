export type Waveform = OscillatorType;

export interface Envelope {
  attack: number;
  decay: number;
  sustain: number;
  release: number;
}

export interface SynthSettings {
  oscillator1: { waveform: Waveform; octave: number; tune: number; level: number };
  oscillator2: { waveform: Waveform; octave: number; tune: number; level: number };
  filter: { cutoff: number; resonance: number; contour: number };
  filterEnvelope: Envelope;
  ampEnvelope: Envelope;
  master: number;
  routing: { vco1: boolean; vco2: boolean; filterEnv: boolean; ampEnv: boolean };
}

interface Voice {
  oscillators: OscillatorNode[];
  oscillatorGains: GainNode[];
  filter: BiquadFilterNode;
  amp: GainNode;
  voiceGain: GainNode;
  note: number;
  released: boolean;
  connected: [boolean, boolean];
}

const DEFAULT_SETTINGS: SynthSettings = {
  oscillator1: { waveform: "sawtooth", octave: 0, tune: 0, level: 0.72 },
  oscillator2: { waveform: "square", octave: 0, tune: 7, level: 0.38 },
  filter: { cutoff: 1800, resonance: 0.8, contour: 0.55 },
  filterEnvelope: { attack: 0.025, decay: 0.42, sustain: 0.32, release: 0.38 },
  ampEnvelope: { attack: 0.015, decay: 0.25, sustain: 0.78, release: 0.32 },
  master: 0.72,
  routing: { vco1: true, vco2: true, filterEnv: true, ampEnv: true },
};

const clamp = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

export class SynthEngine {
  private context: AudioContext | null = null;
  private master: GainNode | null = null;
  private voices = new Map<number, Voice>();
  private settings: SynthSettings = structuredClone(DEFAULT_SETTINGS);

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

  noteOn(note: number, velocity = 0.82) {
    if (!this.context || !this.master || this.voices.has(note)) return;
    const context = this.context;
    const now = context.currentTime;
    const voiceGain = context.createGain();
    const filter = context.createBiquadFilter();
    const amp = context.createGain();
    filter.type = "lowpass";
    filter.Q.value = this.settings.filter.resonance;
    amp.gain.setValueAtTime(0, now);
    voiceGain.gain.value = 1;

    const configs = [this.settings.oscillator1, this.settings.oscillator2];
    const oscillators: OscillatorNode[] = [];
    const oscillatorGains: GainNode[] = [];
    for (const config of configs) {
      const oscillator = context.createOscillator();
      const gain = context.createGain();
      oscillator.type = config.waveform;
      oscillator.frequency.value = 440 * 2 ** ((note - 69 + config.octave * 12 + config.tune) / 12);
      gain.gain.value = config.level;
      oscillator.connect(gain);
      oscillator.start(now);
      oscillators.push(oscillator);
      oscillatorGains.push(gain);
    }

    voiceGain.connect(filter);
    filter.connect(amp);
    amp.connect(this.master);
    const voice: Voice = { oscillators, oscillatorGains, filter, amp, voiceGain, note, released: false, connected: [false, false] };
    this.voices.set(note, voice);
    this.updateVoice(voice);

    const ampEnv = this.settings.ampEnvelope;
    const filterEnv = this.settings.filterEnvelope;
    amp.gain.cancelScheduledValues(now);
    if (this.settings.routing.ampEnv) {
      amp.gain.setValueAtTime(0, now);
      amp.gain.linearRampToValueAtTime(velocity, now + ampEnv.attack);
      amp.gain.linearRampToValueAtTime(velocity * ampEnv.sustain, now + ampEnv.attack + ampEnv.decay);
    } else {
      amp.gain.setValueAtTime(velocity, now);
    }

    filter.frequency.cancelScheduledValues(now);
    filter.frequency.setValueAtTime(clamp(this.settings.filter.cutoff, 30, 18000), now);
    if (this.settings.routing.filterEnv) {
      const peak = this.settings.filter.cutoff * 2 ** (this.settings.filter.contour * 3);
      filter.frequency.exponentialRampToValueAtTime(clamp(peak, 31, 19000), now + Math.max(0.005, filterEnv.attack));
      filter.frequency.exponentialRampToValueAtTime(
        clamp(this.settings.filter.cutoff * 2 ** (this.settings.filter.contour * (filterEnv.sustain * 3)), 31, 19000),
        now + filterEnv.attack + filterEnv.decay,
      );
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
    window.setTimeout(() => {
      voice.voiceGain.disconnect();
      voice.filter.disconnect();
      voice.amp.disconnect();
      this.voices.delete(note);
    }, (stopAt - now) * 1000 + 30);
  }

  allNotesOff() {
    for (const note of this.voices.keys()) this.noteOff(note);
  }

  private updateVoice(voice: Voice) {
    if (!this.context) return;
    const configs = [this.settings.oscillator1, this.settings.oscillator2];
    configs.forEach((config, index) => {
      const oscillator = voice.oscillators[index];
      const routeEnabled = index === 0 ? this.settings.routing.vco1 : this.settings.routing.vco2;
      if (routeEnabled && !voice.connected[index]) {
        voice.oscillatorGains[index].connect(voice.voiceGain);
        voice.connected[index] = true;
      } else if (!routeEnabled && voice.connected[index]) {
        voice.oscillatorGains[index].disconnect(voice.voiceGain);
        voice.connected[index] = false;
      }
      oscillator.type = config.waveform;
      oscillator.frequency.setTargetAtTime(
        440 * 2 ** ((voice.note - 69 + config.octave * 12 + config.tune) / 12),
        this.context!.currentTime,
        0.015,
      );
      voice.oscillatorGains[index].gain.setTargetAtTime(config.level, this.context!.currentTime, 0.015);
    });
    voice.filter.Q.setTargetAtTime(this.settings.filter.resonance, this.context.currentTime, 0.015);
    if (!this.settings.routing.filterEnv) voice.filter.frequency.setTargetAtTime(this.settings.filter.cutoff, this.context.currentTime, 0.015);
  }
}
