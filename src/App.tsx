import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties, type PointerEvent as ReactPointerEvent } from "react";
import { AudioLines, Cable, CircleHelp, Power, Volume2 } from "lucide-react";
import { SynthEngine, type Envelope, type SynthSettings, type Waveform } from "./audio/SynthEngine";

const initial: SynthSettings = {
  oscillator1: { waveform: "sawtooth", octave: 0, tune: 0, level: 0.72 },
  oscillator2: { waveform: "square", octave: 0, tune: 7, level: 0.38, hardSync: false, crossMod: 0 },
  lfo: { waveform: "sine", rate: 4.5, pitchDepth: 0, filterDepth: 0 },
  filter: { cutoff: 1800, resonance: 0.8, contour: 0.55 },
  routing: { vco1: true, vco2: true, filterEnv: true, ampEnv: true },
  filterEnvelope: { attack: 0.025, decay: 0.42, sustain: 0.32, release: 0.38 },
  ampEnvelope: { attack: 0.015, decay: 0.25, sustain: 0.78, release: 0.32 },
  master: 0.72,
};

const computerKeys = ["A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J", "K"];
const noteNames = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
const blackSemitones = new Set([1, 3, 6, 8, 10]);
const notes = Array.from({ length: 25 }, (_, index) => ({
  key: computerKeys[index] ?? "",
  note: 60 + index,
  label: noteNames[index % 12],
  black: blackSemitones.has(index % 12),
}));


const microphoneVoiceNoteStart = 127;

function detectFundamental(buffer: Float32Array, sampleRate: number): number | null {
  let rms = 0;
  for (let index = 0; index < buffer.length; index += 1) rms += buffer[index] * buffer[index];
  rms = Math.sqrt(rms / buffer.length);
  if (rms < 0.012) return null;

  const minTau = Math.max(2, Math.floor(sampleRate / 1000));
  const maxTau = Math.min(Math.floor(sampleRate / 55), Math.floor(buffer.length / 2));
  const difference = new Float32Array(maxTau + 1);
  for (let tau = minTau; tau <= maxTau; tau += 1) {
    let sum = 0;
    for (let index = 0; index < buffer.length - maxTau; index += 1) {
      const delta = buffer[index] - buffer[index + tau];
      sum += delta * delta;
    }
    difference[tau] = sum;
  }

  let runningSum = 0;
  for (let tau = minTau; tau <= maxTau; tau += 1) {
    runningSum += difference[tau];
    difference[tau] = runningSum > 0 ? difference[tau] * (tau - minTau + 1) / runningSum : 1;
  }

  let selectedTau = -1;
  for (let tau = minTau; tau <= maxTau; tau += 1) {
    if (difference[tau] < 0.14) {
      while (tau + 1 <= maxTau && difference[tau + 1] < difference[tau]) tau += 1;
      selectedTau = tau;
      break;
    }
  }
  if (selectedTau < 0) return null;

  const left = difference[selectedTau - 1] ?? difference[selectedTau];
  const center = difference[selectedTau];
  const right = difference[selectedTau + 1] ?? center;
  const denominator = 2 * (2 * center - right - left);
  const correction = denominator !== 0 ? (right - left) / denominator : 0;
  const refinedTau = selectedTau + correction;
  const frequency = sampleRate / refinedTau;
  return frequency >= 55 && frequency <= 1000 ? frequency : null;
}

function pitchName(frequency: number) {
  const midi = 69 + 12 * Math.log2(frequency / 440);
  const rounded = Math.round(midi);
  const cents = Math.round((midi - rounded) * 100);
  const names = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
  return { note: `${names[(rounded % 12 + 12) % 12]}${Math.floor(rounded / 12) - 1}`, cents };
}

type MidiInputPort = {
  id: string;
  name?: string | null;
  state?: string;
  onmidimessage: ((event: { data: Uint8Array | null }) => void) | null;
};
type MidiAccessLike = {
  inputs: { values(): IterableIterator<MidiInputPort> };
  onstatechange: (() => void) | null;
};
type MidiNavigator = {
  requestMIDIAccess?: () => Promise<unknown>;
};

type MidiCcMapping = Record<string, number>;
type CcControl = { id: string; label: string; min: number; max: number; step: number; logarithmic?: boolean };
const ccControls: CcControl[] = [
  { id: "oscillator1.octave", label: "VCO 1 · Octave", min: -2, max: 2, step: 1 },
  { id: "oscillator1.tune", label: "VCO 1 · Tune", min: -12, max: 12, step: 1 },
  { id: "oscillator1.level", label: "VCO 1 · Level", min: 0, max: 1, step: 0.01 },
  { id: "oscillator2.octave", label: "VCO 2 · Octave", min: -2, max: 2, step: 1 },
  { id: "oscillator2.tune", label: "VCO 2 · Tune", min: -12, max: 12, step: 1 },
  { id: "oscillator2.level", label: "VCO 2 · Level", min: 0, max: 1, step: 0.01 },
  { id: "oscillator2.crossMod", label: "VCO 2 · FM Amount", min: 0, max: 800, step: 5 },
  { id: "filter.cutoff", label: "Filter · Cutoff", min: 30, max: 18000, step: 10, logarithmic: true },
  { id: "filter.resonance", label: "Filter · Resonance", min: 0.1, max: 18, step: 0.1 },
  { id: "filter.contour", label: "Filter · Contour", min: 0, max: 1, step: 0.01 },
  { id: "filterEnvelope.attack", label: "Filter Env · Attack", min: 0.005, max: 4, step: 0.005, logarithmic: true },
  { id: "filterEnvelope.decay", label: "Filter Env · Decay", min: 0.005, max: 5, step: 0.005, logarithmic: true },
  { id: "filterEnvelope.sustain", label: "Filter Env · Sustain", min: 0, max: 1, step: 0.01 },
  { id: "filterEnvelope.release", label: "Filter Env · Release", min: 0.005, max: 6, step: 0.005, logarithmic: true },
  { id: "ampEnvelope.attack", label: "Amp Env · Attack", min: 0.005, max: 4, step: 0.005, logarithmic: true },
  { id: "ampEnvelope.decay", label: "Amp Env · Decay", min: 0.005, max: 5, step: 0.005, logarithmic: true },
  { id: "ampEnvelope.sustain", label: "Amp Env · Sustain", min: 0, max: 1, step: 0.01 },
  { id: "ampEnvelope.release", label: "Amp Env · Release", min: 0.005, max: 6, step: 0.005, logarithmic: true },
  { id: "lfo.rate", label: "LFO · Rate", min: 0.1, max: 20, step: 0.1 },
  { id: "lfo.pitchDepth", label: "LFO · Pitch Depth", min: 0, max: 25, step: 0.1 },
  { id: "lfo.filterDepth", label: "LFO · Filter Depth", min: 0, max: 5000, step: 10 },
];

type ParamProps = { label: string; value: number; min: number; max: number; step?: number; display?: (v: number) => string; onChange: (v: number) => void; logarithmic?: boolean };

function Knob({ label, value, min, max, step = 0.01, display, onChange }: ParamProps) {
  const angle = -135 + ((value - min) / (max - min)) * 270;
  const drag = useRef<{ pointerId: number; x: number; y: number; value: number; axis: "x" | "y" | null } | null>(null);
  const beginDrag = (event: ReactPointerEvent<HTMLInputElement>) => {
    event.preventDefault();
    event.currentTarget.setPointerCapture(event.pointerId);
    drag.current = { pointerId: event.pointerId, x: event.clientX, y: event.clientY, value, axis: null };
  };
  const moveDrag = (event: ReactPointerEvent<HTMLInputElement>) => {
    const active = drag.current;
    if (!active || active.pointerId !== event.pointerId) return;
    event.preventDefault();
    const dx = event.clientX - active.x;
    const dy = active.y - event.clientY;
    if (!active.axis && Math.max(Math.abs(dx), Math.abs(dy)) >= 3) {
      active.axis = Math.abs(dx) > Math.abs(dy) ? "x" : "y";
    }
    if (!active.axis) return;
    const distance = active.axis === "y" ? dy : dx;
    const next = Math.min(max, Math.max(min, active.value + (distance / 140) * (max - min)));
    const quantized = min + Math.round((next - min) / step) * step;
    onChange(Math.min(max, Math.max(min, quantized)));
  };
  const endDrag = (event: ReactPointerEvent<HTMLInputElement>) => {
    if (drag.current?.pointerId === event.pointerId) drag.current = null;
  };
  return <label className="knob-control">
    <span className="knob-label">{label}</span>
    <span className="knob" style={{ "--angle": `${angle}deg` } as CSSProperties}>
      <input aria-label={label} type="range" min={min} max={max} step={step} value={value}
        onPointerDown={beginDrag} onPointerMove={moveDrag} onPointerUp={endDrag} onPointerCancel={endDrag}
        onChange={(event) => onChange(Number(event.target.value))} />
      <span className="knob-face"><span className="knob-indicator" /></span>
    </span>
    <output>{display ? display(value) : value.toFixed(2)}</output>
  </label>;
}

function Select({ label, value, options, onChange, disabled = false }: { label: string; value: string; options: string[]; onChange: (v: string) => void; disabled?: boolean }) {
  return <label className="select-control"><span>{label}</span><select value={value} disabled={disabled} onChange={(e) => onChange(e.target.value)}>{options.map((option) => <option key={option} value={option}>{option.toUpperCase()}</option>)}</select></label>;
}

function EnvelopePanel({ title, value, onChange, accent }: { title: string; value: Envelope; onChange: (v: Envelope) => void; accent: string }) {
  const fields: { key: keyof Envelope; label: string; min: number; max: number; step: number; format: (n: number) => string }[] = [
    { key: "attack", label: "ATTACK", min: 0.005, max: 4, step: 0.005, format: (n) => `${n.toFixed(2)}s` },
    { key: "decay", label: "DECAY", min: 0.005, max: 5, step: 0.005, format: (n) => `${n.toFixed(2)}s` },
    { key: "sustain", label: "SUSTAIN", min: 0, max: 1, step: 0.01, format: (n) => `${Math.round(n * 100)}%` },
    { key: "release", label: "RELEASE", min: 0.005, max: 6, step: 0.005, format: (n) => `${n.toFixed(2)}s` },
  ];
  return <section className="module envelope-module">
    <div className="module-heading"><span className={`module-led ${accent}`} /><div><span className="eyebrow">MODULATION</span><h2>{title}</h2></div><span className="module-index">ENV / 0{accent === "violet" ? "1" : "2"}</span></div>
    <div className="envelope-visual"><svg viewBox="0 0 260 66" role="img" aria-label={`${title} envelope shape`}><path d="M8 56 L40 8 L104 34 L190 34 L250 56" className={`envelope-line ${accent}`} /><path d="M8 56 L40 8 L104 34 L190 34 L250 56 L250 60 L8 60Z" className={`envelope-fill ${accent}`} /></svg></div>
    <div className="knob-row envelope-knobs">{fields.map((field) => <Knob key={field.key} label={field.label} value={value[field.key]} min={field.min} max={field.max} step={field.step} display={field.format} onChange={(n) => onChange({ ...value, [field.key]: n })} />)}</div>
  </section>;
}

export default function App() {
  const engine = useRef(new SynthEngine());
  const [settings, setSettings] = useState(initial);
  const [powered, setPowered] = useState(false);
  const [held, setHeld] = useState<number[]>([]);
  const routing = settings.routing;
  const [octave, setOctave] = useState(0);
  const [showHelp, setShowHelp] = useState(false);
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [midiConnected, setMidiConnected] = useState(false);
  const [ccMappings, setCcMappings] = useState<MidiCcMapping>(() => {
    try { return JSON.parse(localStorage.getItem("web-play-synth-midi-cc") || "{}") as MidiCcMapping; }
    catch { return {}; }
  });
  const [showCcMapping, setShowCcMapping] = useState(false);
  const [ccTarget, setCcTarget] = useState(ccControls[0].id);
  const [learningCc, setLearningCc] = useState(false);
  const midiCcHandler = useRef<(cc: number, value: number) => void>(() => undefined);
  const sourceNotes = useRef(new Map<string, { note: number; displayNote: number }>());
  const midiAccess = useRef<MidiAccessLike | null>(null);
  const [microphoneActive, setMicrophoneActive] = useState(false);
  const [microphoneStatus, setMicrophoneStatus] = useState("MICROPHONE OFF");
  const [microphonePitch, setMicrophonePitch] = useState<{ frequency: number; note: string; cents: number } | null>(null);
  const microphoneStream = useRef<MediaStream | null>(null);
  const microphoneContext = useRef<AudioContext | null>(null);
  const microphoneFrame = useRef<number | null>(null);
  const microphoneVoiceStarted = useRef(false);
  const microphoneVoiceNote = useRef(microphoneVoiceNoteStart);

  useEffect(() => { engine.current.update(settings); }, [settings]);
  const update = useCallback(<K extends keyof SynthSettings,>(key: K, value: SynthSettings[K]) => {
    setSettings((current) => ({ ...current, [key]: value }));
  }, []);

  useEffect(() => {
    try { localStorage.setItem("web-play-synth-midi-cc", JSON.stringify(ccMappings)); } catch { /* Storage may be unavailable in private browsing. */ }
  }, [ccMappings]);

  midiCcHandler.current = (cc, midiValue) => {
    if (learningCc) {
      setCcMappings((current) => ({ ...Object.fromEntries(Object.entries(current).filter(([key, assigned]) => key === ccTarget || assigned !== cc)), [ccTarget]: cc }));
      setLearningCc(false);
      return;
    }
    const target = ccControls.find((control) => ccMappings[control.id] === cc);
    if (!target) return;
    const normalized = midiValue / 127;
    const raw = target.logarithmic
      ? target.min * Math.pow(target.max / target.min, normalized)
      : target.min + normalized * (target.max - target.min);
    const next = Math.min(target.max, Math.max(target.min,
      target.min + Math.round((raw - target.min) / target.step) * target.step));
    setSettings((current) => {
      switch (target.id) {
        case "oscillator1.octave": return { ...current, oscillator1: { ...current.oscillator1, octave: next } };
        case "oscillator1.tune": return { ...current, oscillator1: { ...current.oscillator1, tune: next } };
        case "oscillator1.level": return { ...current, oscillator1: { ...current.oscillator1, level: next } };
        case "oscillator2.octave": return { ...current, oscillator2: { ...current.oscillator2, octave: next } };
        case "oscillator2.tune": return { ...current, oscillator2: { ...current.oscillator2, tune: next } };
        case "oscillator2.level": return { ...current, oscillator2: { ...current.oscillator2, level: next } };
        case "oscillator2.crossMod": return { ...current, oscillator2: { ...current.oscillator2, crossMod: next } };
        case "filter.cutoff": return { ...current, filter: { ...current.filter, cutoff: next } };
        case "filter.resonance": return { ...current, filter: { ...current.filter, resonance: next } };
        case "filter.contour": return { ...current, filter: { ...current.filter, contour: next } };
        case "filterEnvelope.attack": return { ...current, filterEnvelope: { ...current.filterEnvelope, attack: next } };
        case "filterEnvelope.decay": return { ...current, filterEnvelope: { ...current.filterEnvelope, decay: next } };
        case "filterEnvelope.sustain": return { ...current, filterEnvelope: { ...current.filterEnvelope, sustain: next } };
        case "filterEnvelope.release": return { ...current, filterEnvelope: { ...current.filterEnvelope, release: next } };
        case "ampEnvelope.attack": return { ...current, ampEnvelope: { ...current.ampEnvelope, attack: next } };
        case "ampEnvelope.decay": return { ...current, ampEnvelope: { ...current.ampEnvelope, decay: next } };
        case "ampEnvelope.sustain": return { ...current, ampEnvelope: { ...current.ampEnvelope, sustain: next } };
        case "ampEnvelope.release": return { ...current, ampEnvelope: { ...current.ampEnvelope, release: next } };
        case "lfo.rate": return { ...current, lfo: { ...current.lfo, rate: next } };
        case "lfo.pitchDepth": return { ...current, lfo: { ...current.lfo, pitchDepth: next } };
        case "lfo.filterDepth": return { ...current, lfo: { ...current.lfo, filterDepth: next } };
        default: return current;
      }
    });
  };
  const playSource = useCallback(async (source: string, note: number, velocity = 0.82, displayNote = note) => {
    if (sourceNotes.current.has(source)) return;
    sourceNotes.current.set(source, { note, displayNote });
    await engine.current.start();
    const pitchIsStillHeld = [...sourceNotes.current.values()].some((entry) => entry.note === note);
    if (pitchIsStillHeld) engine.current.noteOn(note, velocity);
    if (!sourceNotes.current.has(source)) return;
    setPowered(true);
    setHeld((current) => current.includes(displayNote) ? current : [...current, displayNote]);
  }, []);
  const releaseSource = useCallback((source: string) => {
    const entry = sourceNotes.current.get(source);
    if (!entry) return;
    sourceNotes.current.delete(source);
    const pitchStillHeld = [...sourceNotes.current.values()].some((active) => active.note === entry.note);
    if (!pitchStillHeld) engine.current.noteOff(entry.note);
    const displayStillHeld = [...sourceNotes.current.values()].some((active) => active.displayNote === entry.displayNote);
    if (!displayStillHeld) setHeld((current) => current.filter((n) => n !== entry.displayNote));
  }, []);
  const play = useCallback((note: number) => {
    void playSource(`piano:${note}`, note + octave * 12, 0.82, note);
  }, [octave, playSource]);
  const release = useCallback((note: number) => releaseSource(`piano:${note}`), [releaseSource]);

  useEffect(() => {
    const keyMap = new Map(notes.filter((item) => item.key).map((item) => [item.key.toLowerCase(), item.note]));
    const down = (event: KeyboardEvent) => {
      if (event.repeat || event.ctrlKey || event.metaKey || event.altKey) return;
      const target = event.target;
      if (target instanceof HTMLElement && (target.isContentEditable || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement || (target instanceof HTMLInputElement && !["range", "checkbox", "button"].includes(target.type)))) return;
      const key = event.key.toLowerCase();
      const note = keyMap.get(key);
      if (note !== undefined) {
        event.preventDefault();
        void playSource(`computer:${key}`, note + octave * 12, 0.82, note);
      }
    };
    const up = (event: KeyboardEvent) => {
      releaseSource(`computer:${event.key.toLowerCase()}`);
    };
    const blur = () => {
      for (const source of [...sourceNotes.current.keys()]) {
        if (source.startsWith("computer:")) releaseSource(source);
      }
    };
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", blur);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
      window.removeEventListener("blur", blur);
    };
  }, [octave, playSource, releaseSource]);

  useEffect(() => {
    const access = midiAccess.current;
    if (!access) return;
    const refreshDevices = () => {
      const inputs = [...access.inputs.values()];
      setMidiDevices(inputs.filter((input) => input.state !== "disconnected").map((input) => input.name || "Unnamed MIDI keyboard"));
    };
    access.onstatechange = () => {
      for (const input of access.inputs.values()) {
        if (input.state === "disconnected") {
          input.onmidimessage = null;
          for (const source of [...sourceNotes.current.keys()]) {
            if (source.startsWith(`midi:${input.id}:`)) releaseSource(source);
          }
        } else {
          input.onmidimessage = (event) => {
            const data = event.data;
            if (!data) return;
            const status = data[0];
            const data1 = data[1];
            const data2 = data[2];
            if (status === undefined || data1 === undefined) return;
            const command = status & 0xf0;
            if (command === 0xb0) { midiCcHandler.current(data1, data2 ?? 0); return; }
            const channel = status & 0x0f;
            const source = `midi:${input.id}:${channel}:${data1}`;
            if (command === 0x90 && (data2 ?? 0) > 0) {
              void playSource(source, data1, (data2 ?? 0) / 127, data1);
            } else if (command === 0x80 || (command === 0x90 && (data2 ?? 0) === 0)) {
              releaseSource(source);
            }
          };
        }
      }
      refreshDevices();
    };
    refreshDevices();
    return () => {
      access.onstatechange = null;
      for (const input of access.inputs.values()) input.onmidimessage = null;
    };
  }, [midiConnected, playSource, releaseSource]);

  const connectMidi = async () => {
    const midiNavigator = navigator as MidiNavigator;
    if (!midiNavigator.requestMIDIAccess) return;
    try {
      const access = await midiNavigator.requestMIDIAccess() as MidiAccessLike;
      midiAccess.current = access;
      for (const input of access.inputs.values()) {
        input.onmidimessage = (event) => {
          const data = event.data;
          if (!data) return;
          const status = data[0];
          const data1 = data[1];
          const data2 = data[2];
          if (status === undefined || data1 === undefined) return;
          const command = status & 0xf0;
          if (command === 0xb0) { midiCcHandler.current(data1, data2 ?? 0); return; }
          const channel = status & 0x0f;
          const source = `midi:${input.id}:${channel}:${data1}`;
          if (command === 0x90 && (data2 ?? 0) > 0) {
            void playSource(source, data1, (data2 ?? 0) / 127, data1);
          } else if (command === 0x80 || (command === 0x90 && (data2 ?? 0) === 0)) {
            releaseSource(source);
          }
        };
      }
      setMidiConnected(true);
      setMidiDevices([...access.inputs.values()].map((input) => input.name || "Unnamed MIDI keyboard"));
    } catch (error) {
      console.error("Unable to access MIDI devices", error);
      setMidiConnected(false);
    }
  };
  const stopMicrophone = useCallback(() => {
    if (microphoneFrame.current !== null) cancelAnimationFrame(microphoneFrame.current);
    microphoneFrame.current = null;
    microphoneStream.current?.getTracks().forEach((track) => track.stop());
    microphoneStream.current = null;
    void microphoneContext.current?.close();
    microphoneContext.current = null;
    if (microphoneVoiceStarted.current) {
      engine.current.noteOff(microphoneVoiceNote.current);
      microphoneVoiceStarted.current = false;
    }
    setMicrophoneActive(false);
    setMicrophonePitch(null);
    setMicrophoneStatus("MICROPHONE OFF");
  }, []);

  const startMicrophone = async () => {
    if (!navigator.mediaDevices?.getUserMedia) {
      setMicrophoneStatus("MICROPHONE INPUT REQUIRES HTTPS OR LOCALHOST");
      return;
    }
    setMicrophoneStatus("REQUESTING MICROPHONE PERMISSION…");
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false },
      });
      microphoneStream.current = stream;
      const context = new AudioContext();
      microphoneContext.current = context;
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048;
      analyser.smoothingTimeConstant = 0;
      const input = context.createMediaStreamSource(stream);
      input.connect(analyser);
      const silentOutput = context.createGain();
      silentOutput.gain.value = 0;
      analyser.connect(silentOutput);
      silentOutput.connect(context.destination);
      await context.resume();
      await engine.current.start();
      setPowered(true);
      microphoneVoiceNote.current += 1;
      engine.current.noteOn(microphoneVoiceNote.current, 0, 440);
      microphoneVoiceStarted.current = true;
      setMicrophoneActive(true);
      setMicrophoneStatus("LISTENING · HUM OR SING A NOTE");

      const samples = new Float32Array(analyser.fftSize);
      let lastUiUpdate = 0;
      let lastFrequency = 0;
      let silenceSince = 0;
      let lastAnalysis = 0;
      let gateOpen = false;
      const tick = (time: number) => {
        if (time - lastAnalysis >= 35) {
          lastAnalysis = time;
          analyser.getFloatTimeDomainData(samples);
          const frequency = detectFundamental(samples, context.sampleRate);
          if (frequency !== null) {
            silenceSince = 0;
            if (Math.abs(frequency - lastFrequency) > 0.8) {
              engine.current.setVoicePitch(microphoneVoiceNote.current, frequency);
              lastFrequency = frequency;
            }
            if (!gateOpen) {
              engine.current.setVoiceGate(microphoneVoiceNote.current, true);
              gateOpen = true;
            }
            if (time - lastUiUpdate > 120) {
              const named = pitchName(frequency);
              setMicrophonePitch({ frequency, ...named });
              setMicrophoneStatus("PITCH TRACKING");
              lastUiUpdate = time;
            }
          } else {
            if (!silenceSince) silenceSince = time;
            if (time - silenceSince > 140 && gateOpen) {
              engine.current.setVoiceGate(microphoneVoiceNote.current, false);
              gateOpen = false;
            }
            if (time - lastUiUpdate > 180) {
              setMicrophonePitch(null);
              setMicrophoneStatus("LISTENING · HUM OR SING A NOTE");
              lastUiUpdate = time;
            }
          }
        }
        microphoneFrame.current = requestAnimationFrame(tick);
      };
      microphoneFrame.current = requestAnimationFrame(tick);
    } catch (error) {
      console.error("Unable to start microphone pitch input", error);
      microphoneStream.current?.getTracks().forEach((track) => track.stop());
      microphoneStream.current = null;
      void microphoneContext.current?.close();
      microphoneContext.current = null;
      if (microphoneVoiceStarted.current) engine.current.noteOff(microphoneVoiceNote.current);
      microphoneVoiceStarted.current = false;
      setMicrophoneActive(false);
      setMicrophoneStatus(error instanceof DOMException && error.name === "NotAllowedError"
        ? "MICROPHONE PERMISSION DENIED"
        : "COULD NOT START MICROPHONE INPUT");
    }
  };

  const disconnectMidi = () => {
    const access = midiAccess.current;
    if (access) {
      access.onstatechange = null;
      for (const input of access.inputs.values()) input.onmidimessage = null;
    }
    for (const source of [...sourceNotes.current.keys()]) {
      if (source.startsWith("midi:")) releaseSource(source);
    }
    midiAccess.current = null;
    setMidiDevices([]);
    setMidiConnected(false);
  };

  useEffect(() => () => {
    sourceNotes.current.clear();
    engine.current.allNotesOff();
    const access = midiAccess.current;
    if (access) {
      access.onstatechange = null;
      for (const input of access.inputs.values()) input.onmidimessage = null;
    }
  }, []);
  const activeCount = held.length;
  useEffect(() => () => {
    if (microphoneFrame.current !== null) cancelAnimationFrame(microphoneFrame.current);
    microphoneStream.current?.getTracks().forEach((track) => track.stop());
    if (microphoneVoiceStarted.current) engine.current.noteOff(microphoneVoiceNote.current);
    microphoneVoiceStarted.current = false;
    void microphoneContext.current?.close();
  }, []);

  const frequencyLabel = useMemo(() => `${Math.round(settings.filter.cutoff).toLocaleString()} Hz`, [settings.filter.cutoff]);

  return <main className="app-shell">
    <header className="topbar">
      <a className="brand" href="#"><span className="brand-mark"><AudioLines size={19} /></span><span>WEB PLAY <b>SYNTH</b></span></a>
      <div className="topbar-center"><span className="status-dot" /> VIRTUAL ANALOG SYSTEM <span className="topbar-divider">/</span> PATCH 001</div>
      <div className="top-actions"><button className="icon-button" aria-label="Help" onClick={() => setShowHelp(!showHelp)}><CircleHelp size={18} /></button><button className={`power-button ${powered ? "is-on" : ""}`} onClick={async () => { if (!powered) { await engine.current.start(); setPowered(true); } else { sourceNotes.current.clear(); engine.current.allNotesOff(); setHeld([]); } }}><Power size={15} /> {powered ? "AUDIO READY" : "POWER ON"}</button></div>
    </header>

    <section className="hero">
      <div><div className="eyebrow">INSTRUMENT / 001 — SUBTRACTIVE SYNTHESIS</div><h1>THE <em>DUAL VCO</em></h1><p>Two oscillators. One signal path. Infinite possibilities.</p></div>
      <div className="hero-readout"><span>VOICE STATUS</span><strong>{activeCount ? `${activeCount.toString().padStart(2, "0")} ACTIVE` : "STANDBY"}</strong><small>{powered ? "AUDIO ENGINE RUNNING" : "AWAITING AUDIO ACTIVATION"}</small></div>
    </section>

    {showHelp && <aside className="help-banner"><b>Quick start</b> — Press POWER ON, then play the on-screen keyboard or use A–K (white keys) and W/E/T/Y/U (black keys). Shape the tone with the VCOs, filter, envelopes, and LFO modulation controls.<button onClick={() => setShowHelp(false)}>DISMISS</button></aside>}

    <div className="section-kicker"><span>01</span> OSCILLATORS & MIXER <i /></div>
    <section className="upper-rack">
      <div className="module oscillator-module">
        <div className="module-heading"><span className="module-led cyan" /><div><span className="eyebrow">VOLTAGE CONTROLLED OSCILLATOR</span><h2>VCO 01</h2></div><span className="module-index">OSC / 01</span></div>
        <Select label="WAVEFORM" value={settings.oscillator1.waveform} options={["sawtooth", "square", "triangle", "sine"]} onChange={(v) => update("oscillator1", { ...settings.oscillator1, waveform: v as Waveform })} />
        <div className="knob-row"><Knob label="OCTAVE" value={settings.oscillator1.octave} min={-2} max={2} step={1} display={(n) => `${n > 0 ? "+" : ""}${n}`} onChange={(n) => update("oscillator1", { ...settings.oscillator1, octave: n })} /><Knob label="TUNE" value={settings.oscillator1.tune} min={-12} max={12} display={(n) => `${n > 0 ? "+" : ""}${n} st`} onChange={(n) => update("oscillator1", { ...settings.oscillator1, tune: n })} /><Knob label="LEVEL" value={settings.oscillator1.level} min={0} max={1} display={(n) => `${Math.round(n * 100)}%`} onChange={(n) => update("oscillator1", { ...settings.oscillator1, level: n })} /></div>
        <div className="wave-preview"><svg viewBox="0 0 240 35" preserveAspectRatio="none"><path d={settings.oscillator1.waveform === "sawtooth" ? "M0 30 L60 5 L60 30 L120 5 L120 30 L180 5 L180 30 L240 5" : settings.oscillator1.waveform === "square" ? "M0 27 L0 8 L60 8 L60 27 L120 27 L120 8 L180 8 L180 27 L240 27" : settings.oscillator1.waveform === "triangle" ? "M0 27 L30 5 L60 27 L90 5 L120 27 L150 5 L180 27 L210 5 L240 27" : "M0 17 C15 -2 45 -2 60 17 S105 36 120 17 S165 -2 180 17 S225 36 240 17"} /></svg><span>WAVEFORM MONITOR</span></div>
      </div>

      <div className="mixer-module module">
        <div className="module-heading"><span className="module-led amber" /><div><span className="eyebrow">SIGNAL SUMMING</span><h2>MIXER</h2></div><span className="module-index">MIX / 01</span></div>
        <div className="mixer-channel"><span className="channel-name">01 <b>VCO 1</b></span><div className="meter-track"><span style={{ width: `${settings.oscillator1.level * 100}%` }} /></div><strong>{Math.round(settings.oscillator1.level * 100)}</strong></div>
        <div className="mixer-channel"><span className="channel-name">02 <b>VCO 2</b></span><div className="meter-track second"><span style={{ width: `${settings.oscillator2.level * 100}%` }} /></div><strong>{Math.round(settings.oscillator2.level * 100)}</strong></div>
        <div className="mixer-sum"><span>SUM OUTPUT</span><strong>{Math.round((settings.oscillator1.level + settings.oscillator2.level) * 50)}<small>%</small></strong></div>
        <div className="mixer-note"><Cable size={14} /> BOTH OSCILLATORS ROUTED TO FILTER BUS</div>
      </div>

      <div className="module oscillator-module">
        <div className="module-heading"><span className="module-led violet" /><div><span className="eyebrow">VOLTAGE CONTROLLED OSCILLATOR</span><h2>VCO 02</h2></div><span className="module-index">OSC / 02</span></div>
        <Select label="WAVEFORM" value={settings.oscillator2.waveform} options={["sawtooth", "square", "triangle", "sine"]} disabled={settings.oscillator2.hardSync} onChange={(v) => update("oscillator2", { ...settings.oscillator2, waveform: v as Waveform })} />
        <div className="knob-row"><Knob label="OCTAVE" value={settings.oscillator2.octave} min={-2} max={2} step={1} display={(n) => `${n > 0 ? "+" : ""}${n}`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, octave: n })} /><Knob label="TUNE" value={settings.oscillator2.tune} min={-12} max={12} display={(n) => `${n > 0 ? "+" : ""}${n} st`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, tune: n })} /><Knob label="LEVEL" value={settings.oscillator2.level} min={0} max={1} display={(n) => `${Math.round(n * 100)}%`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, level: n })} /></div>
        <div className="wave-preview violet-wave"><svg viewBox="0 0 240 35" preserveAspectRatio="none"><path d={settings.oscillator2.waveform === "sawtooth" ? "M0 30 L60 5 L60 30 L120 5 L120 30 L180 5 L180 30 L240 5" : settings.oscillator2.waveform === "square" ? "M0 27 L0 8 L60 8 L60 27 L120 27 L120 8 L180 8 L180 27 L240 27" : settings.oscillator2.waveform === "triangle" ? "M0 27 L30 5 L60 27 L90 5 L120 27 L150 5 L180 27 L210 5 L240 27" : "M0 17 C15 -2 45 -2 60 17 S105 36 120 17 S165 -2 180 17 S225 36 240 17"} /></svg><span>{settings.oscillator2.hardSync ? "HARD-SYNC WAVEFORM" : "WAVEFORM MONITOR"}</span></div>
        <div className="vco2-mod-controls">
          <div className="vco2-mod-heading"><span>VCO 2 MODES</span><small>Controls that change how VCO 2 interacts with VCO 1</small></div>
          <button type="button" className={`sync-button ${settings.oscillator2.hardSync ? "is-active" : ""}`} aria-pressed={settings.oscillator2.hardSync} onClick={() => update("oscillator2", { ...settings.oscillator2, hardSync: !settings.oscillator2.hardSync })}>
            <span className="sync-indicator" />
            <span><strong>HARD SYNC</strong><small>Reset VCO 2 at each VCO 1 cycle; overrides its waveform</small></span>
            <b>{settings.oscillator2.hardSync ? "ON" : "OFF"}</b>
          </button>
          <div className="vco2-fm-control">
            <Knob label="FM AMOUNT" value={settings.oscillator2.crossMod} min={0} max={800} step={5} display={(n) => `${Math.round(n)} Hz`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, crossMod: n })} />
            <p>At zero, VCO 2 does not modulate VCO 1. Turn this up for a brighter, more metallic tone.</p>
          </div>
        </div>
      </div>
    </section>

    <div className="section-kicker"><span>02</span> FILTER & MODULATION <i /></div>
    <section className="lower-rack">
      <div className="module filter-module">
        <div className="module-heading"><span className="module-led amber" /><div><span className="eyebrow">VOLTAGE CONTROLLED FILTER</span><h2>VCF — LOW PASS</h2></div><span className="module-index">FLT / 01</span></div>
        <div className="filter-display"><div className="filter-display-top"><span>CUTOFF FREQUENCY</span><strong>{frequencyLabel}</strong></div><svg viewBox="0 0 340 105" preserveAspectRatio="none"><path className="filter-grid" d="M0 20H340 M0 52H340 M0 84H340 M40 0V105 M100 0V105 M160 0V105 M220 0V105 M280 0V105" /><path className="filter-curve" d={`M0 10 C${Math.max(70, settings.filter.cutoff / 80)} 10 ${Math.max(95, settings.filter.cutoff / 30)} 10 ${Math.max(120, settings.filter.cutoff / 15)} 55 S${Math.min(290, settings.filter.cutoff / 5)} 95 340 98`} /></svg><div className="filter-display-bottom"><span>20 Hz</span><span>1 kHz</span><span>20 kHz</span></div></div>
        <div className="knob-row filter-knobs"><Knob label="CUTOFF" value={settings.filter.cutoff} min={30} max={18000} step={10} display={(n) => n > 999 ? `${(n / 1000).toFixed(1)}k` : `${Math.round(n)} Hz`} onChange={(n) => update("filter", { ...settings.filter, cutoff: n })} /><Knob label="RESONANCE" value={settings.filter.resonance} min={0.1} max={18} display={(n) => n.toFixed(1)} onChange={(n) => update("filter", { ...settings.filter, resonance: n })} /><Knob label="CONTOUR" value={settings.filter.contour} min={0} max={1} display={(n) => `${Math.round(n * 100)}%`} onChange={(n) => update("filter", { ...settings.filter, contour: n })} /></div>
        <div className="filter-footer"><span>24 DB / OCT</span><span>ENVELOPE AMOUNT <b>{Math.round(settings.filter.contour * 100)}%</b></span></div>
      </div>
      <EnvelopePanel title="FILTER ENVELOPE" value={settings.filterEnvelope} accent="violet" onChange={(v) => update("filterEnvelope", v)} />
      <EnvelopePanel title="AMPLIFIER ENVELOPE" value={settings.ampEnvelope} accent="cyan" onChange={(v) => update("ampEnvelope", v)} />
    </section>

    <div className="section-kicker"><span>03</span> LFO MODULATION <i /></div>
    <section className="module modulation-module">
      <div className="module-heading"><span className="module-led violet" /><div><span className="eyebrow">LOW FREQUENCY OSCILLATOR</span><h2>LFO ROUTING</h2></div><span className="module-index">LFO / 01</span></div>
      <p className="modulation-intro">The LFO is an automatic, repeating control signal. Choose its speed and waveform, then set how strongly it moves pitch or filter cutoff. A depth of zero means that destination is off.</p>
      <div className="modulation-controls">
        <Select label="LFO WAVEFORM" value={settings.lfo.waveform} options={["sine", "triangle", "square", "sawtooth"]} onChange={(v) => update("lfo", { ...settings.lfo, waveform: v as Waveform })} />
        <div className="knob-row">
          <Knob label="RATE" value={settings.lfo.rate} min={0.1} max={20} step={0.1} display={(n) => `${n.toFixed(1)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, rate: n })} />
          <Knob label="PITCH DEPTH" value={settings.lfo.pitchDepth} min={0} max={25} step={0.1} display={(n) => `${n.toFixed(1)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, pitchDepth: n })} />
          <Knob label="FILTER DEPTH" value={settings.lfo.filterDepth} min={0} max={5000} step={10} display={(n) => `${Math.round(n)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, filterDepth: n })} />
        </div>
      </div>
      <div className="lfo-destination-hints">
        <span><b>PITCH</b> gently bends both oscillator pitches</span>
        <span><b>FILTER</b> sweeps the low-pass cutoff</span>
      </div>
    </section>

    <div className="section-kicker"><span>04</span> SIGNAL FLOW <i /></div>
    <section className="module routing-module">
      <div className="module-heading"><span className="module-led cyan" /><div><span className="eyebrow">FIXED AUDIO PATH + ROUTE SWITCHES</span><h2>AUDIO ROUTING</h2></div><span className="module-index">PATCH / 001</span></div>
      <div className="signal-flow" aria-label="Main audio signal path">
        <div className="signal-stage"><span>01</span><strong>OSCILLATORS</strong><small>VCO 1 + VCO 2</small></div>
        <span className="signal-arrow">→</span>
        <div className="signal-stage"><span>02</span><strong>MIXER</strong><small>Audio sum</small></div>
        <span className="signal-arrow">→</span>
        <div className="signal-stage"><span>03</span><strong>VCF</strong><small>Low-pass filter</small></div>
        <span className="signal-arrow">→</span>
        <div className="signal-stage"><span>04</span><strong>VCA</strong><small>Final level</small></div>
        <span className="signal-arrow">→</span>
        <div className="signal-output"><span className="output-led" /> OUTPUT</div>
      </div>
      <div className="route-groups">
        <div className="route-group">
          <div className="route-group-heading"><span className="route-group-index">A</span><div><strong>AUDIO CONNECTIONS</strong><small>Oscillators feed the mixer</small></div></div>
          <div className="route-list">
            <div className={"route-row " + (routing.vco1 ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot cyan" /> VCO 01</span><span className="route-type">AUDIO</span><span className="route-destination">MIXER INPUT 1</span>
              <button className="route-switch" aria-pressed={routing.vco1} onClick={() => update("routing", { ...routing, vco1: !routing.vco1 })}>{routing.vco1 ? "CONNECTED" : "BYPASSED"}</button>
            </div>
            <div className={"route-row " + (routing.vco2 ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot violet" /> VCO 02</span><span className="route-type">AUDIO</span><span className="route-destination">MIXER INPUT 2</span>
              <button className="route-switch" aria-pressed={routing.vco2} onClick={() => update("routing", { ...routing, vco2: !routing.vco2 })}>{routing.vco2 ? "CONNECTED" : "BYPASSED"}</button>
            </div>
          </div>
        </div>
        <div className="route-group">
          <div className="route-group-heading"><span className="route-group-index">B</span><div><strong>ENVELOPE CONTROL VOLTAGE</strong><small>Envelopes shape filter cutoff and amplifier level</small></div></div>
          <div className="route-list">
            <div className={"route-row " + (routing.filterEnv ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot violet" /> FILTER ENV</span><span className="route-type">CV</span><span className="route-destination">VCF CUTOFF</span>
              <button className="route-switch" aria-pressed={routing.filterEnv} onClick={() => update("routing", { ...routing, filterEnv: !routing.filterEnv })}>{routing.filterEnv ? "CONNECTED" : "BYPASSED"}</button>
            </div>
            <div className={"route-row " + (routing.ampEnv ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot cyan" /> AMP ENV</span><span className="route-type">CV</span><span className="route-destination">VCA LEVEL</span>
              <button className="route-switch" aria-pressed={routing.ampEnv} onClick={() => update("routing", { ...routing, ampEnv: !routing.ampEnv })}>{routing.ampEnv ? "CONNECTED" : "BYPASSED"}</button>
            </div>
          </div>
        </div>
        <div className="route-group route-group-modulation">
          <div className="route-group-heading"><span className="route-group-index">C</span><div><strong>MODULATION STATUS</strong><small>These connections are controlled by the LFO and VCO 2 controls above</small></div></div>
          <div className="route-list">
            <div className={"route-row " + (settings.lfo.pitchDepth > 0 ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot violet" /> LFO</span><span className="route-type">PITCH</span><span className="route-destination">VCO 1 + VCO 2</span>
              <span className={"route-state " + (settings.lfo.pitchDepth > 0 ? "is-active" : "")}>{settings.lfo.pitchDepth > 0 ? `ACTIVE · ±${settings.lfo.pitchDepth.toFixed(1)} Hz` : "DEPTH 0"}</span>
            </div>
            <div className={"route-row " + (settings.lfo.filterDepth > 0 ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot violet" /> LFO</span><span className="route-type">CUTOFF</span><span className="route-destination">VCF FREQUENCY</span>
              <span className={"route-state " + (settings.lfo.filterDepth > 0 ? "is-active" : "")}>{settings.lfo.filterDepth > 0 ? `ACTIVE · ${settings.lfo.filterDepth.toFixed(0)} Hz` : "DEPTH 0"}</span>
            </div>
            <div className={"route-row " + (settings.oscillator2.crossMod > 0 ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot cyan" /> VCO 2</span><span className="route-type">FM</span><span className="route-destination">VCO 1 FREQUENCY</span>
              <span className={"route-state " + (settings.oscillator2.crossMod > 0 ? "is-active" : "")}>{settings.oscillator2.crossMod > 0 ? `ACTIVE · ${settings.oscillator2.crossMod.toFixed(0)} Hz` : "DEPTH 0"}</span>
            </div>
            <div className={"route-row " + (settings.oscillator2.hardSync ? "is-connected" : "")}>
              <span className="route-source"><i className="route-dot cyan" /> VCO 1</span><span className="route-type">SYNC</span><span className="route-destination">RESETS VCO 2</span>
              <span className={"route-state " + (settings.oscillator2.hardSync ? "is-active" : "")}>{settings.oscillator2.hardSync ? "ENABLED" : "DISABLED"}</span>
            </div>
          </div>
        </div>
      </div>
      <p className="routing-footnote">No cables need to be dragged. Audio and envelope switches change the audio path; modulation is enabled automatically when its depth is above zero or hard sync is switched on.</p>
    </section>

    <div className="section-kicker"><span>05</span> PERFORMANCE <i /></div>
    <section className="module keyboard-module">
      <div className="keyboard-heading"><div><span className="eyebrow">PLAYABLE INPUT</span><h2>KEYBOARD</h2></div><div className="octave-control"><button onClick={() => setOctave(Math.max(-2, octave - 1))}>−</button><span>OCTAVE <b>{octave > 0 ? "+" : ""}{octave}</b></span><button onClick={() => setOctave(Math.min(2, octave + 1))}>+</button></div></div>
      <div className="midi-panel">
        <div><span className="eyebrow">HARDWARE INPUT</span><strong>MIDI KEYBOARD</strong><small>{midiConnected ? `${midiDevices.length} INPUT(S) DETECTED` : "CONNECT A USB OR BLUETOOTH MIDI CONTROLLER"}</small></div>
        <div className="midi-panel-actions">
          <button className="midi-button" onClick={midiConnected ? disconnectMidi : connectMidi}>{midiConnected ? "DISCONNECT MIDI" : "CONNECT MIDI"}</button>
          <button className="midi-map-toggle" aria-expanded={showCcMapping} onClick={() => setShowCcMapping((shown) => !shown)}>CC MAP <span>{Object.keys(ccMappings).length}</span></button>
        </div>
        {midiConnected && <span className="midi-device-list">{midiDevices.length ? midiDevices.join(" · ") : "Waiting for MIDI input…"}</span>}
        {showCcMapping && <div className="cc-mapping-panel">
          <div className="cc-map-topline"><strong>MIDI CONTROL MAP</strong><span>{Object.keys(ccMappings).length} ASSIGNED</span></div>
          <div className="cc-map-assign">
            <label><span>PARAMETER</span><select value={ccTarget} onChange={(event) => { setCcTarget(event.target.value); setLearningCc(false); }}>{ccControls.map((control) => <option key={control.id} value={control.id}>{control.label}</option>)}</select></label>
            <button className={learningCc ? "is-learning" : ""} disabled={!midiConnected} onClick={() => setLearningCc(true)}>{learningCc ? "MOVE A CONTROL…" : "LEARN CC"}</button>
          </div>
          <p className="cc-map-hint">{!midiConnected ? "Connect a MIDI controller to assign controls." : learningCc ? "Move a knob or fader on your MIDI controller." : "Select a parameter, then learn its CC. Assignments are saved in this browser."}</p>
          {Object.keys(ccMappings).length > 0 && <div className="cc-map-list">{ccControls.filter((control) => ccMappings[control.id] !== undefined).map((control) => <div className="cc-map-row" key={control.id}>
            <span>{control.label}</span><b>CC {ccMappings[control.id]}</b><button aria-label={`Remove ${control.label} mapping`} onClick={() => setCcMappings((current) => { const next = { ...current }; delete next[control.id]; return next; })}>×</button>
          </div>)}</div>}
        </div>}
        {!((navigator as MidiNavigator).requestMIDIAccess) && <span className="midi-unsupported">Web MIDI is unavailable in this browser. Try a Chromium-based browser over HTTPS or localhost.</span>}
      </div>
      <div className={`microphone-panel ${microphoneActive ? "is-active" : ""}`}>
        <div className="microphone-info"><span className="eyebrow">ALTERNATE PITCH INPUT</span><strong>MICROPHONE → VCO</strong><small>{microphonePitch ? `${microphonePitch.note} · ${microphonePitch.frequency.toFixed(1)} HZ · ${microphonePitch.cents > 0 ? "+" : ""}${microphonePitch.cents}¢` : microphoneStatus}</small></div>
        <button className="microphone-button" onClick={microphoneActive ? stopMicrophone : startMicrophone}>{microphoneActive ? "STOP MIC" : "ENABLE MIC"}</button>
      </div>
      <div className="piano">{notes.filter((n) => !n.black).map((note) => <div className="white-key-wrap" key={note.note}><button className={`white-key ${held.includes(note.note) ? "pressed" : ""}`} onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); void play(note.note); }} onPointerUp={() => release(note.note)} onPointerLeave={() => held.includes(note.note) && release(note.note)}><span>{note.label}</span>{note.key && <small>{note.key}</small>}</button>{notes.find((n) => n.note === note.note + 1)?.black && <button className={`black-key ${held.includes(note.note + 1) ? "pressed" : ""}`} onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); void play(note.note + 1); }} onPointerUp={() => release(note.note + 1)} onPointerLeave={() => held.includes(note.note + 1) && release(note.note + 1)}><span>{notes.find((n) => n.note === note.note + 1)!.key}</span></button>}</div>)}</div>
      <div className="keyboard-footer"><span>COMPUTER KEYS · FIRST OCTAVE A–K / W E T Y U · TWO-OCTAVE TOUCH KEYBOARD</span><span><Volume2 size={14} /> {powered ? "AUDIO ACTIVE" : "PRESS POWER TO ENABLE AUDIO"}</span></div>
    </section>
    <footer><span>WEB PLAY SYNTH <b>001</b></span><span>LOCAL AUDIO ENGINE · NO SERVER-SIDE AUDIO</span><span>BUILT FOR EXPERIMENTATION <AudioLines size={13} /></span></footer>
  </main>;
}
