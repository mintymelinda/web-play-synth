import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
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

const notes = [
  { key: "A", note: 60, label: "C" }, { key: "W", note: 61, label: "C♯", black: true },
  { key: "S", note: 62, label: "D" }, { key: "E", note: 63, label: "D♯", black: true },
  { key: "D", note: 64, label: "E" }, { key: "F", note: 65, label: "F" },
  { key: "T", note: 66, label: "F♯", black: true }, { key: "G", note: 67, label: "G" },
  { key: "Y", note: 68, label: "G♯", black: true }, { key: "H", note: 69, label: "A" },
  { key: "U", note: 70, label: "A♯", black: true }, { key: "J", note: 71, label: "B" },
  { key: "K", note: 72, label: "C" },
];

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

type ParamProps = { label: string; value: number; min: number; max: number; step?: number; display?: (v: number) => string; onChange: (v: number) => void; logarithmic?: boolean };

function Knob({ label, value, min, max, step = 0.01, display, onChange, logarithmic = false }: ParamProps) {
  const angle = -135 + ((value - min) / (max - min)) * 270;
  return <label className="knob-control">
    <span className="knob-label">{label}</span>
    <span className="knob" style={{ "--angle": `${angle}deg` } as CSSProperties}>
      <input aria-label={label} type="range" min={min} max={max} step={step} value={value}
        onChange={(event) => onChange(logarithmic ? Number(event.target.value) : Number(event.target.value))} />
      <span className="knob-face"><span className="knob-indicator" /></span>
    </span>
    <output>{display ? display(value) : value.toFixed(2)}</output>
  </label>;
}

function Select({ label, value, options, onChange }: { label: string; value: string; options: string[]; onChange: (v: string) => void }) {
  return <label className="select-control"><span>{label}</span><select value={value} onChange={(e) => onChange(e.target.value)}>{options.map((option) => <option key={option} value={option}>{option.toUpperCase()}</option>)}</select></label>;
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
  const [dragSource, setDragSource] = useState<string | null>(null);
  const [patchMessage, setPatchMessage] = useState("Drag a source jack onto a destination to make a connection.");
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [midiConnected, setMidiConnected] = useState(false);
  const sourceNotes = useRef(new Map<string, { note: number; displayNote: number }>());
  const midiAccess = useRef<MidiAccessLike | null>(null);

  const connectPatch = (destination: string, source: string | null = dragSource) => {
    if (!source) return;
    const routes: Record<string, keyof SynthSettings["routing"]> = { "vco1:mixer": "vco1", "vco2:mixer": "vco2", "filterEnv:vcf": "filterEnv", "ampEnv:vca": "ampEnv" };
    const route = routes[`${source}:${destination}`];
    if (route) { update("routing", { ...settings.routing, [route]: true }); setPatchMessage(`${source.toUpperCase()} connected to ${destination.toUpperCase()}.`); }
    else setPatchMessage("That connection is not supported by this patchbay yet.");
    setDragSource(null);
  };

  useEffect(() => { engine.current.update(settings); }, [settings]);
  const update = useCallback(<K extends keyof SynthSettings,>(key: K, value: SynthSettings[K]) => {
    setSettings((current) => ({ ...current, [key]: value }));
  }, []);
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
    const keyMap = new Map(notes.map((item) => [item.key.toLowerCase(), item.note]));
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
        <Select label="WAVEFORM" value={settings.oscillator2.waveform} options={["sawtooth", "square", "triangle", "sine"]} onChange={(v) => update("oscillator2", { ...settings.oscillator2, waveform: v as Waveform })} />
        <div className="knob-row"><Knob label="OCTAVE" value={settings.oscillator2.octave} min={-2} max={2} step={1} display={(n) => `${n > 0 ? "+" : ""}${n}`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, octave: n })} /><Knob label="TUNE" value={settings.oscillator2.tune} min={-12} max={12} display={(n) => `${n > 0 ? "+" : ""}${n} st`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, tune: n })} /><Knob label="LEVEL" value={settings.oscillator2.level} min={0} max={1} display={(n) => `${Math.round(n * 100)}%`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, level: n })} /></div>
        <div className="wave-preview violet-wave"><svg viewBox="0 0 240 35" preserveAspectRatio="none"><path d={settings.oscillator2.waveform === "sawtooth" ? "M0 30 L60 5 L60 30 L120 5 L120 30 L180 5 L180 30 L240 5" : settings.oscillator2.waveform === "square" ? "M0 27 L0 8 L60 8 L60 27 L120 27 L120 8 L180 8 L180 27 L240 27" : settings.oscillator2.waveform === "triangle" ? "M0 27 L30 5 L60 27 L90 5 L120 27 L150 5 L180 27 L210 5 L240 27" : "M0 17 C15 -2 45 -2 60 17 S105 36 120 17 S165 -2 180 17 S225 36 240 17"} /></svg><span>WAVEFORM MONITOR</span></div>
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

    <div className="section-kicker"><span>03</span> LFO & OSCILLATOR MODULATION <i /></div>
    <section className="module modulation-module">
      <div className="module-heading"><span className="module-led violet" /><div><span className="eyebrow">LOW FREQUENCY OSCILLATOR</span><h2>MODULATION SOURCE</h2></div><span className="module-index">LFO / 01</span></div>
      <div className="modulation-controls">
        <Select label="LFO WAVEFORM" value={settings.lfo.waveform} options={["sine", "triangle", "square", "sawtooth"]} onChange={(v) => update("lfo", { ...settings.lfo, waveform: v as Waveform })} />
        <div className="knob-row">
          <Knob label="RATE" value={settings.lfo.rate} min={0.1} max={20} step={0.1} display={(n) => `${n.toFixed(1)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, rate: n })} />
          <Knob label="PITCH DEPTH" value={settings.lfo.pitchDepth} min={0} max={25} step={0.1} display={(n) => `${n.toFixed(1)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, pitchDepth: n })} />
          <Knob label="FILTER DEPTH" value={settings.lfo.filterDepth} min={0} max={5000} step={10} display={(n) => `${Math.round(n)} Hz`} onChange={(n) => update("lfo", { ...settings.lfo, filterDepth: n })} />
        </div>
      </div>
      <div className="oscillator-modulation">
        <label className="sync-toggle"><input type="checkbox" checked={settings.oscillator2.hardSync} onChange={(event) => update("oscillator2", { ...settings.oscillator2, hardSync: event.target.checked })} /><span className="toggle-dot violet" /> HARD SYNC <small>VCO 2 RESETS TO VCO 1</small></label>
        <Knob label="CROSS MOD" value={settings.oscillator2.crossMod} min={0} max={800} step={5} display={(n) => `${Math.round(n)} Hz`} onChange={(n) => update("oscillator2", { ...settings.oscillator2, crossMod: n })} />
        <p>VCO 2 modulates VCO 1 pitch. Hard sync locks VCO 2's waveform cycle to VCO 1 for bright, cutting harmonics.</p>
      </div>
    </section>

    <div className="section-kicker"><span>04</span> MODULAR ROUTING <i /></div>
    <section className="module routing-module">
      <div className="module-heading"><span className="module-led cyan" /><div><span className="eyebrow">PATCH BAY / SIGNAL FLOW</span><h2>ROUTING MATRIX</h2></div><span className="module-index">PATCH / 001</span></div>
      <div className="patchbay">
        <div className="patch-column">
          <span className="patch-label">SOURCES <small>DRAG A JACK</small></span>
          <div className={`patch-node cyan-node draggable-node ${dragSource === "vco1" ? "is-dragging" : ""}`} draggable onDragStart={(event) => { event.dataTransfer.setData("text/plain", "vco1"); setDragSource("vco1"); }} onDragEnd={() => setDragSource(null)}><span className="port" /> VCO 01 <small>AUDIO</small></div>
          <div className={`patch-node violet-node draggable-node ${dragSource === "vco2" ? "is-dragging" : ""}`} draggable onDragStart={(event) => { event.dataTransfer.setData("text/plain", "vco2"); setDragSource("vco2"); }} onDragEnd={() => setDragSource(null)}><span className="port" /> VCO 02 <small>AUDIO</small></div>
          <div className={`patch-node violet-node draggable-node ${dragSource === "filterEnv" ? "is-dragging" : ""}`} draggable onDragStart={(event) => { event.dataTransfer.setData("text/plain", "filterEnv"); setDragSource("filterEnv"); }} onDragEnd={() => setDragSource(null)}><span className="port" /> FILTER ENV <small>CV</small></div>
          <div className={`patch-node cyan-node draggable-node ${dragSource === "ampEnv" ? "is-dragging" : ""}`} draggable onDragStart={(event) => { event.dataTransfer.setData("text/plain", "ampEnv"); setDragSource("ampEnv"); }} onDragEnd={() => setDragSource(null)}><span className="port" /> AMP ENV <small>CV</small></div>
        </div>
        <div className="patch-cables"><svg viewBox="0 0 420 230" preserveAspectRatio="none" aria-label="Live patch cable connections">
          <path className={routing.vco1 ? "cable cable-cyan" : "cable cable-muted"} d="M0 35 C150 35 150 85 420 85" />
          <path className={routing.vco2 ? "cable cable-violet" : "cable cable-muted"} d="M0 85 C150 85 150 85 420 85" />
          <path className={routing.filterEnv ? "cable cable-violet modulation" : "cable cable-muted"} d="M0 135 C150 135 150 40 420 40" />
          <path className={routing.ampEnv ? "cable cable-cyan modulation" : "cable cable-muted"} d="M0 185 C150 185 150 145 420 145" />
        </svg><span className="cable-caption">PATCH CONNECTIONS</span></div>
        <div className="patch-column destinations">
          <span className="patch-label">DESTINATIONS <small>DROP TO CONNECT</small></span>
          <div className={`patch-node amber-node drop-target ${dragSource ? "can-receive" : ""}`} onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); connectPatch("mixer", event.dataTransfer.getData("text/plain") || dragSource); }} onDoubleClick={() => { update("routing", { ...routing, vco1: false, vco2: false }); setPatchMessage("Mixer audio inputs disconnected."); }}><span className="port" /> MIXER <small>{routing.vco1 || routing.vco2 ? "CONNECTED" : "AUDIO IN"}</small></div>
          <div className={`patch-node amber-node drop-target ${dragSource ? "can-receive" : ""}`} onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); connectPatch("vcf", event.dataTransfer.getData("text/plain") || dragSource); }} onDoubleClick={() => { update("routing", { ...routing, filterEnv: false }); setPatchMessage("Filter envelope disconnected."); }}><span className="port" /> VCF <small>{routing.filterEnv ? "CONNECTED" : "CUTOFF CV"}</small></div>
          <div className={`patch-node amber-node drop-target ${dragSource ? "can-receive" : ""}`} onDragOver={(event) => event.preventDefault()} onDrop={(event) => { event.preventDefault(); connectPatch("vca", event.dataTransfer.getData("text/plain") || dragSource); }} onDoubleClick={() => { update("routing", { ...routing, ampEnv: false }); setPatchMessage("Amplifier envelope disconnected."); }}><span className="port" /> VCA <small>{routing.ampEnv ? "CONNECTED" : "GAIN CV"}</small></div>
        </div>
      </div>
      <p className="patch-status" role="status">{patchMessage} <span>Double-click a destination to disconnect its inputs.</span></p>
      <div className="route-toggles">
        <label><input type="checkbox" checked={routing.vco1} onChange={(e) => update("routing", { ...routing, vco1: e.target.checked })} /><span className="toggle-dot cyan" /> VCO 1 → MIXER</label>
        <label><input type="checkbox" checked={routing.vco2} onChange={(e) => update("routing", { ...routing, vco2: e.target.checked })} /><span className="toggle-dot violet" /> VCO 2 → MIXER</label>
        <label><input type="checkbox" checked={routing.filterEnv} onChange={(e) => update("routing", { ...routing, filterEnv: e.target.checked })} /><span className="toggle-dot violet" /> FILTER ENV → CUTOFF</label>
        <label><input type="checkbox" checked={routing.ampEnv} onChange={(e) => update("routing", { ...routing, ampEnv: e.target.checked })} /><span className="toggle-dot cyan" /> AMP ENV → VCA</label>
      </div>
      <p className="routing-footnote">Toggle a connection to patch or unpatch the route. The matrix is the visual representation of the current signal architecture.</p>
    </section>

    <div className="section-kicker"><span>05</span> PERFORMANCE <i /></div>
    <section className="module keyboard-module">
      <div className="keyboard-heading"><div><span className="eyebrow">PLAYABLE INPUT</span><h2>KEYBOARD</h2></div><div className="octave-control"><button onClick={() => setOctave(Math.max(-2, octave - 1))}>−</button><span>OCTAVE <b>{octave > 0 ? "+" : ""}{octave}</b></span><button onClick={() => setOctave(Math.min(2, octave + 1))}>+</button></div></div>
      <div className="midi-panel">
        <div><span className="eyebrow">HARDWARE INPUT</span><strong>MIDI KEYBOARD</strong><small>{midiConnected ? `${midiDevices.length} INPUT(S) DETECTED` : "CONNECT A USB OR BLUETOOTH MIDI CONTROLLER"}</small></div>
        <button className="midi-button" onClick={midiConnected ? disconnectMidi : connectMidi}>{midiConnected ? "DISCONNECT MIDI" : "CONNECT MIDI"}</button>
        {midiConnected && <span className="midi-device-list">{midiDevices.length ? midiDevices.join(" · ") : "Waiting for MIDI input…"}</span>}
        {!((navigator as MidiNavigator).requestMIDIAccess) && <span className="midi-unsupported">Web MIDI is unavailable in this browser. Try a Chromium-based browser over HTTPS or localhost.</span>}
      </div>
      <div className="piano">{notes.filter((n) => !n.black).map((note) => <div className="white-key-wrap" key={note.note}><button className={`white-key ${held.includes(note.note) ? "pressed" : ""}`} onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); void play(note.note); }} onPointerUp={() => release(note.note)} onPointerLeave={() => held.includes(note.note) && release(note.note)}><span>{note.label}</span><small>{note.key}</small></button>{notes.find((n) => n.note === note.note + 1)?.black && <button className={`black-key ${held.includes(note.note + 1) ? "pressed" : ""}`} onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); void play(note.note + 1); }} onPointerUp={() => release(note.note + 1)} onPointerLeave={() => held.includes(note.note + 1) && release(note.note + 1)}><span>{notes.find((n) => n.note === note.note + 1)!.key}</span></button>}</div>)}</div>
      <div className="keyboard-footer"><span>COMPUTER KEYBOARD ACTIVE · A–K / W E T Y U</span><span><Volume2 size={14} /> {powered ? "AUDIO ACTIVE" : "PRESS POWER TO ENABLE AUDIO"}</span></div>
    </section>
    <footer><span>WEB PLAY SYNTH <b>001</b></span><span>LOCAL AUDIO ENGINE · NO SERVER-SIDE AUDIO</span><span>BUILT FOR EXPERIMENTATION <AudioLines size={13} /></span></footer>
  </main>;
}
