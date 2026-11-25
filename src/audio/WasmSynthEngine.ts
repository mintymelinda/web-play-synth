import type { SynthSettings } from "./SynthEngine";

declare global { interface Window { __WPS_ENGINE_BACKEND__?: string; } }
type ActiveVoice = { midiNote: number; channel: number; velocity: number };
const waveformIndex: Record<string, number> = { sine: 0, triangle: 1, sawtooth: 2, square: 3 };

function encodeParameters(s: SynthSettings): Float32Array {
  const p=new Float32Array(52);
  p[0]=waveformIndex[s.oscillator1.waveform]??2; p[1]=waveformIndex[s.oscillator2.waveform]??3;
  p[2]=s.oscillator1.octave; p[3]=s.oscillator1.tune; p[4]=s.oscillator1.level; p[5]=.5;
  p[6]=s.oscillator2.octave; p[7]=s.oscillator2.tune; p[8]=s.oscillator2.level; p[9]=.5;
  p[10]=s.oscillator2.hardSync?1:0; p[11]=s.oscillator2.crossMod;
  p[12]=s.filter.cutoff; p[13]=s.filter.resonance; p[14]=s.filter.contour;
  p[18]=s.filterEnvelope.attack; p[19]=s.filterEnvelope.decay; p[20]=s.filterEnvelope.sustain; p[21]=s.filterEnvelope.release;
  p[22]=s.ampEnvelope.attack; p[23]=s.ampEnvelope.decay; p[24]=s.ampEnvelope.sustain; p[25]=s.ampEnvelope.release;
  p[26]=waveformIndex[s.lfo.waveform]??0; p[27]=s.lfo.rate; p[28]=s.lfo.pitchDepth; p[29]=s.lfo.filterDepth;
  p[33]=s.master; p[37]=.8; p[38]=.5; p[40]=.32; p[41]=.35; p[44]=.55; p[45]=.4;
  p[46]=s.glide.enabled?1:0; p[47]=s.glide.time;
  p[48]=s.routing.vco1?1:0; p[49]=s.routing.vco2?1:0; p[50]=s.routing.filterEnv?1:0; p[51]=s.routing.ampEnv?1:0;
  return p;
}

export class WasmSynthEngine {
  private context: AudioContext|null=null;
  private node: AudioWorkletNode|null=null;
  private settings: SynthSettings|null=null;
  private readonly voices=new Map<number,ActiveVoice>();
  private revision=0;

  get isReady(){return this.context?.state==="running"&&this.node!==null;}

  async start(){
    if(!this.context){
      const context=new AudioContext({latencyHint:"interactive"});
      this.context=context;
      const resumed=context.resume();
      try{
        await context.audioWorklet.addModule("/synth-worklet.js");
        const node=new AudioWorkletNode(context,"web-play-synth",{
          numberOfInputs:0,numberOfOutputs:1,outputChannelCount:[2],channelCount:2,channelCountMode:"explicit",
        });
        this.node=node; node.connect(context.destination);
        await new Promise<void>((resolve,reject)=>{
          const timeout=window.setTimeout(()=>reject(new Error("Timed out loading the shared WebAssembly synth engine")),15000);
          node.port.onmessage=(event:MessageEvent)=>{
            if(event.data?.type==="ready"){window.clearTimeout(timeout);window.__WPS_ENGINE_BACKEND__="wasm";resolve();}
            else if(event.data?.type==="error"){window.clearTimeout(timeout);reject(new Error(String(event.data.message??"WebAssembly synth initialization failed")));}
          };
        });
        await resumed;
      }catch(error){
        this.node?.disconnect(); this.node=null; this.context=null;
        await context.close().catch(()=>undefined);
        throw error;
      }
    }else if(this.context.state!=="running") await this.context.resume();
    this.sendParameters();
  }

  update(settings:SynthSettings){this.settings=settings;this.sendParameters();}

  noteOn(note:number,velocity=.82,pitchFrequency:number|null=null){
    if(!this.node||this.voices.has(note))return;
    const channel=note===127?16:1;
    const midiNote=pitchFrequency&&Number.isFinite(pitchFrequency)
      ?Math.max(0,Math.min(127,Math.round(69+12*Math.log2(pitchFrequency/440))))
      :Math.max(0,Math.min(127,note));
    this.voices.set(note,{midiNote,channel,velocity});
    this.sendEvent(0,midiNote,channel,velocity);
    if(pitchFrequency!==null)this.setVoicePitch(note,pitchFrequency);
  }

  setVoicePitch(note:number,frequency:number){
    const voice=this.voices.get(note);
    if(!voice||!Number.isFinite(frequency)||frequency<=0)return;
    let semitones=12*Math.log2(frequency/(440*2**((voice.midiNote-69)/12)));
    if(Math.abs(semitones)>1.95){
      const next=Math.max(0,Math.min(127,Math.round(69+12*Math.log2(frequency/440))));
      if(next!==voice.midiNote){
        this.sendEvent(1,voice.midiNote,voice.channel,0);
        voice.midiNote=next;
        this.sendEvent(0,next,voice.channel,voice.velocity);
        semitones=12*Math.log2(frequency/(440*2**((voice.midiNote-69)/12)));
      }
    }
    this.sendEvent(2,0,voice.channel,Math.max(-1,Math.min(1,semitones/2)));
  }

  setVoiceGate(note:number,open:boolean,velocity=.82){
    if(open){const voice=this.voices.get(note);if(voice){voice.velocity=velocity;this.sendEvent(0,voice.midiNote,voice.channel,velocity);}else this.noteOn(note,velocity);}
    else this.noteOff(note);
  }

  noteOff(note:number){
    const voice=this.voices.get(note);if(!voice)return;
    this.sendEvent(1,voice.midiNote,voice.channel,0);this.voices.delete(note);
    if(voice.channel===16)this.sendEvent(2,0,16,0);
  }

  allNotesOff(){this.sendEvent(6,0,1,0);this.sendEvent(6,0,16,0);this.voices.clear();}

  private sendParameters(){
    if(this.node&&this.settings)this.node.port.postMessage({type:"parameters",values:encodeParameters(this.settings),revision:++this.revision});
  }
  private sendEvent(type:number,note:number,channel:number,value:number){
    this.node?.port.postMessage({type:"event",eventType:type,note,channel,value});
  }
}
