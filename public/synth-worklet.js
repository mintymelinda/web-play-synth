const PARAMETER_COUNT=52,MAX_FRAMES=2048,EVENT_CAPACITY=512;
class WebPlaySynthProcessor extends AudioWorkletProcessor{
  constructor(options){
    super();this.ready=false;this.parameters=new Float32Array(PARAMETER_COUNT);
    this.parameters[0]=2;this.parameters[1]=3;this.parameters[4]=.72;this.parameters[8]=.38;
    this.parameters[12]=1800;this.parameters[13]=.8;this.parameters[14]=.55;
    this.parameters[18]=.025;this.parameters[19]=.42;this.parameters[20]=.32;this.parameters[21]=.38;
    this.parameters[22]=.015;this.parameters[23]=.25;this.parameters[24]=.78;this.parameters[25]=.32;
    this.parameters[27]=4.5;this.parameters[33]=.72;this.parameters[37]=.8;this.parameters[38]=.5;
    this.parameters[40]=.32;this.parameters[41]=.35;this.parameters[44]=.55;this.parameters[45]=.4;
    this.parameters[48]=1;this.parameters[49]=1;this.parameters[50]=1;this.parameters[51]=1;
    this.eventTypes=new Uint32Array(EVENT_CAPACITY);this.eventNotes=new Uint32Array(EVENT_CAPACITY);
    this.eventChannels=new Uint32Array(EVENT_CAPACITY);this.eventValues=new Float32Array(EVENT_CAPACITY);
    this.eventCount=0;this.revision=1n;this.port.onmessage=e=>this.handleMessage(e.data);
    this.initialize(options.processorOptions?.wasmBytes);
  }
  async initialize(wasmBytes){
    try{
      if(!(wasmBytes instanceof ArrayBuffer))throw new Error("The browser host did not provide the WebAssembly module bytes");
      const module=await WebAssembly.compile(wasmBytes),imports={};
      for(const entry of WebAssembly.Module.imports(module)){
        const group=imports[entry.module]??(imports[entry.module]={});
        if(entry.kind==="memory")group[entry.name]=new WebAssembly.Memory({initial:256,maximum:256});
        else if(entry.kind==="table")group[entry.name]=new WebAssembly.Table({initial:1024,element:"anyfunc"});
        else if(entry.kind==="global")group[entry.name]=new WebAssembly.Global({value:"i32",mutable:entry.name==="__stack_pointer"},0);
        else if(entry.kind==="function"&&(entry.name==="abort"||entry.name==="__stack_chk_fail"))
          group[entry.name]=()=>{throw new Error("WebAssembly synth aborted: "+entry.name);};
        else if(entry.kind==="function"&&entry.name==="emscripten_notify_memory_growth")group[entry.name]=()=>{};
        else throw new Error("Unsupported WebAssembly import: "+entry.module+"."+entry.name);
      }
      const result=await WebAssembly.instantiate(module,imports);this.instance=result instanceof WebAssembly.Instance?result:result.instance;
      const ex=this.instance.exports,get=n=>ex[n]??ex["_"+n],initialize=get("_initialize");
      this.memory=ex.memory??Object.values(imports).map(g=>g.memory).find(Boolean);
      if(typeof initialize==="function")initialize();
      if(!this.memory||!get("wps_abi_version")||!get("wps_parameter_count")||!get("wps_event_size")||
         !get("wps_prepare")||!get("wps_render")||!get("wps_alloc"))throw new Error("WebAssembly synth exports are incomplete");
      if(get("wps_abi_version")()!==1||get("wps_parameter_count")()!==PARAMETER_COUNT||get("wps_event_size")()!==20)
        throw new Error("WebAssembly synth ABI does not match this browser host");
      this.prepare=get("wps_prepare");this.render=get("wps_render");this.allocate=get("wps_alloc");
      this.parameterPointer=this.allocate(PARAMETER_COUNT*4);this.eventPointer=this.allocate(EVENT_CAPACITY*20);
      this.leftPointer=this.allocate(MAX_FRAMES*4);this.rightPointer=this.allocate(MAX_FRAMES*4);
      if(!this.parameterPointer||!this.eventPointer||!this.leftPointer||!this.rightPointer)throw new Error("Unable to allocate WebAssembly audio buffers");
      const status=this.prepare(sampleRate,MAX_FRAMES,2);if(status!==0)throw new Error("WebAssembly synth prepare failed: "+status);
      this.parameterView=new Float32Array(this.memory.buffer,this.parameterPointer,PARAMETER_COUNT);
      this.dataView=new DataView(this.memory.buffer);this.leftView=new Float32Array(this.memory.buffer,this.leftPointer,MAX_FRAMES);
      this.rightView=new Float32Array(this.memory.buffer,this.rightPointer,MAX_FRAMES);
      this.ready=true;this.port.postMessage({type:"ready"});
    }catch(error){const message=error instanceof Error?error.message:String(error);this.port.postMessage({type:"error",message});}
  }
  handleMessage(message){
    if(!message)return;
    if(message.type==="parameters"&&message.values?.length===PARAMETER_COUNT){this.parameters.set(message.values);this.revision=BigInt(message.revision??1);}
    else if(message.type==="event"){
      if(this.eventCount>=EVENT_CAPACITY){this.port.postMessage({type:"error",message:"MIDI event queue overflow"});return;}
      const i=this.eventCount++;this.eventTypes[i]=message.eventType>>>0;this.eventNotes[i]=message.note>>>0;
      this.eventChannels[i]=message.channel>>>0;this.eventValues[i]=Number(message.value);
    }
  }
  process(inputs,outputs){
    const output=outputs[0];if(!output||output.length===0)return true;
    const left=output[0],right=output[1]??output[0];
    if(!this.ready||left.length>MAX_FRAMES){left.fill(0);if(right!==left)right.fill(0);return true;}
    this.parameterView.set(this.parameters);
    for(let i=0;i<this.eventCount;i++){
      const offset=this.eventPointer+i*20;this.dataView.setUint32(offset,this.eventTypes[i],true);
      this.dataView.setUint32(offset+4,0,true);this.dataView.setUint32(offset+8,this.eventChannels[i],true);
      this.dataView.setUint32(offset+12,this.eventNotes[i],true);this.dataView.setFloat32(offset+16,this.eventValues[i],true);
    }
    const count=this.eventCount,status=this.render(this.parameterPointer,PARAMETER_COUNT,this.revision,
      count?this.eventPointer:0,count,this.leftPointer,this.rightPointer,left.length);
    this.eventCount=0;
    if(status!==0){this.port.postMessage({type:"render-error",status,eventCount:count});left.fill(0);if(right!==left)right.fill(0);return true;}
    for(let i=0;i<left.length;i++){left[i]=this.leftView[i];if(right!==left)right[i]=this.rightView[i];}
    return true;
  }
}
registerProcessor("web-play-synth",WebPlaySynthProcessor);
