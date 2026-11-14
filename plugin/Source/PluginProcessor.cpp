#include "PluginProcessor.h"

namespace {
juce::StringArray waveNames() { return { "Sine", "Triangle", "Saw", "Square" }; }
juce::AudioParameterFloatAttributes cutoffSkew() { return juce::AudioParameterFloatAttributes().withCentreSkew(1200.0f); }
}

WebPlaySynthAudioProcessor::WebPlaySynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"SYNTH_STATE",createParameterLayout()) {}

WebPlaySynthAudioProcessor::ParameterState::ParameterLayout WebPlaySynthAudioProcessor::createParameterLayout()
{
    ParameterState::ParameterLayout l;
    const auto f=[&](const char* id,const char* name,float lo,float hi,float v,float step=0.0f,juce::AudioParameterFloatAttributes a={}) {
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step),v,a));
    };
    const auto b=[&](const char* id,const char* name,bool v) { l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},name,v)); };
    const auto c=[&](const char* id,const char* name,const juce::StringArray& choices,int v) { l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,choices,v)); };
    c("osc1Wave","VCO 1 Waveform",waveNames(),2); c("osc2Wave","VCO 2 Waveform",waveNames(),3);
    f("osc1Octave","VCO 1 Octave",-2,2,0,1); f("osc1Tune","VCO 1 Tune",-12,12,0,1); f("osc1Level","VCO 1 Level",0,1,.72f);
    f("osc2Octave","VCO 2 Octave",-2,2,0,1); f("osc2Tune","VCO 2 Tune",-12,12,7,1); f("osc2Level","VCO 2 Level",0,1,.38f);
    b("hardSync","VCO 2 Hard Sync",false); f("crossMod","VCO Cross Modulation",0,800,0,.1f);
    f("cutoff","Filter Cutoff",30,18000,1800,1,cutoffSkew()); f("resonance","Filter Resonance",.1f,18,.8f,.01f); f("filterContour","Filter Contour",0,1,.55f);
    f("filterAttack","Filter Envelope Attack",.005f,4,.025f,.001f); f("filterDecay","Filter Envelope Decay",.005f,5,.42f,.001f); f("filterSustain","Filter Envelope Sustain",0,1,.32f); f("filterRelease","Filter Envelope Release",.005f,6,.38f,.001f);
    f("ampAttack","Amp Envelope Attack",.005f,4,.015f,.001f); f("ampDecay","Amp Envelope Decay",.005f,5,.25f,.001f); f("ampSustain","Amp Envelope Sustain",0,1,.78f); f("ampRelease","Amp Envelope Release",.005f,6,.32f,.001f);
    c("lfoWave","LFO Waveform",waveNames(),0); f("lfoRate","LFO Rate",.1f,20,4.5f,.01f); f("lfoPitchDepth","LFO Pitch Depth",0,25,0,.01f); f("lfoFilterDepth","LFO Filter Depth",0,5000,0,1);
    f("master","Master Level",0,1,.72f); b("glideEnabled","Global Glide",false); f("glideTime","Glide Time",.01f,2,.18f,.01f);
    b("vco1Enabled","VCO 1 Audio Route",true); b("vco2Enabled","VCO 2 Audio Route",true); b("filterEnvEnabled","Filter Envelope Route",true); b("ampEnvEnabled","Amp Envelope Route",true);
    return l;
}

void WebPlaySynthAudioProcessor::Voice::prepare(double sr,int block)
{
    filter.prepare({sr,static_cast<juce::uint32>(block),1});
    filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass); filter.reset();
    ampEnvelope.setSampleRate(sr); filterEnvelope.setSampleRate(sr);
}
void WebPlaySynthAudioProcessor::Voice::start(int n,float vel,const Parameters& p,double previous)
{
    note=n; velocity=vel; keyDown=true; active=true; phase1=phase2=lfoPhase=0;
    targetFrequency=juce::MidiMessage::getMidiNoteInHertz(n);
    currentFrequency=p.glideEnabled && previous>0 ? previous : targetFrequency;
    ampEnvelope.setParameters({p.ampAttack,p.ampDecay,p.ampSustain,p.ampRelease});
    filterEnvelope.setParameters({p.filterAttack,p.filterDecay,p.filterSustain,p.filterRelease});
    ampEnvelope.noteOn(); filterEnvelope.noteOn(); filter.reset();
}
void WebPlaySynthAudioProcessor::Voice::stop()
{
    if(active && keyDown) { keyDown=false; ampEnvelope.noteOff(); filterEnvelope.noteOff(); }
}
float WebPlaySynthAudioProcessor::Voice::render(const Parameters& p,double sr)
{
    if(!active) return 0;
    if(!keyDown && !ampEnvelope.isActive()) { active=false; note=-1; return 0; }
    const auto glide=p.glideEnabled ? 1.0-std::exp(-1.0/(static_cast<double>(p.glideTime)*sr)) : 1.0;
    currentFrequency+=(targetFrequency-currentFrequency)*glide;
    const auto base1=currentFrequency*std::pow(2.0,(p.osc1Octave*12.0+p.osc1Tune)/12.0);
    const auto base2=currentFrequency*std::pow(2.0,(p.osc2Octave*12.0+p.osc2Tune)/12.0);
    const auto lfo=waveformSample(lfoPhase,p.lfoWave); lfoPhase+=p.lfoRate/sr; if(lfoPhase>=1) lfoPhase-=std::floor(lfoPhase);
    const auto mod=static_cast<double>(lfo*p.lfoPitchDepth);
    const auto f1=juce::jlimit(10.0,20000.0,base1+mod), f2=juce::jlimit(10.0,20000.0,base2+mod);
    const auto inc2=f2/sr;
    const auto s2=oscillatorSample(phase2,inc2,p.osc2Wave);
    const auto fm=juce::jlimit(10.0,20000.0,f1+s2*p.crossMod);
    const auto s1=oscillatorSample(phase1,fm/sr,p.osc1Wave);
    const auto oldPhase=phase1; phase1+=fm/sr; if(phase1>=1) phase1-=std::floor(phase1);
    phase2+=inc2; if(phase2>=1) phase2-=std::floor(phase2);
    if(p.hardSync && phase1<oldPhase) phase2=0;
    const auto mix=s1*p.osc1Level*(p.vco1Enabled?1.0f:0.0f)+s2*p.osc2Level*(p.vco2Enabled?1.0f:0.0f);
    const auto amp=p.ampEnvEnabled?ampEnvelope.getNextSample():1.0f;
    const auto env=p.filterEnvEnabled?filterEnvelope.getNextSample():0.0f;
    const auto cutoff=juce::jlimit(30.0f,19000.0f,p.cutoff*static_cast<float>(std::pow(2.0,p.filterContour*env*3.0))+lfo*p.lfoFilterDepth);
    filter.setCutoffFrequency(cutoff); filter.setResonance(p.resonance);
    return filter.processSample(0,mix)*amp*velocity;
}

void WebPlaySynthAudioProcessor::prepareToPlay(double sr,int block)
{
    currentSampleRate=sr; lastPlayedFrequency=0;
    for(auto& v:voices) v.prepare(sr,block);
}
void WebPlaySynthAudioProcessor::releaseResources() { for(auto& v:voices) { v.active=false; v.keyDown=false; } }
bool WebPlaySynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out=l.getMainOutputChannelSet(); return out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo();
}
WebPlaySynthAudioProcessor::Parameters WebPlaySynthAudioProcessor::readParameters() const
{
    const auto v=[this](const char* id){return parameters.getRawParameterValue(id)->load();}; Parameters p;
    p.osc1Wave=juce::roundToInt(v("osc1Wave")); p.osc2Wave=juce::roundToInt(v("osc2Wave")); p.osc1Octave=v("osc1Octave"); p.osc1Tune=v("osc1Tune"); p.osc1Level=v("osc1Level");
    p.osc2Octave=v("osc2Octave"); p.osc2Tune=v("osc2Tune"); p.osc2Level=v("osc2Level"); p.hardSync=v("hardSync")>.5f; p.crossMod=v("crossMod");
    p.cutoff=v("cutoff"); p.resonance=v("resonance"); p.filterContour=v("filterContour");
    p.filterAttack=v("filterAttack"); p.filterDecay=v("filterDecay"); p.filterSustain=v("filterSustain"); p.filterRelease=v("filterRelease");
    p.ampAttack=v("ampAttack"); p.ampDecay=v("ampDecay"); p.ampSustain=v("ampSustain"); p.ampRelease=v("ampRelease");
    p.lfoWave=juce::roundToInt(v("lfoWave")); p.lfoRate=v("lfoRate"); p.lfoPitchDepth=v("lfoPitchDepth"); p.lfoFilterDepth=v("lfoFilterDepth");
    p.master=v("master"); p.glideEnabled=v("glideEnabled")>.5f; p.glideTime=v("glideTime");
    p.vco1Enabled=v("vco1Enabled")>.5f; p.vco2Enabled=v("vco2Enabled")>.5f; p.filterEnvEnabled=v("filterEnvEnabled")>.5f; p.ampEnvEnabled=v("ampEnvEnabled")>.5f;
    return p;
}
void WebPlaySynthAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard; b.clear(); const auto p=readParameters(); int pos=0;
    for(const auto meta:midi) { const auto at=juce::jlimit(pos,b.getNumSamples(),meta.samplePosition); renderRange(b,pos,at,p); handleMidiMessage(meta.getMessage(),p); pos=at; }
    renderRange(b,pos,b.getNumSamples(),p); b.applyGain(p.master);
}
void WebPlaySynthAudioProcessor::renderRange(juce::AudioBuffer<float>& b,int start,int end,const Parameters& p)
{
    for(int i=start;i<end;++i) { float out=0; for(auto& v:voices) out+=v.render(p,currentSampleRate); for(int ch=0;ch<b.getNumChannels();++ch) b.addSample(ch,i,out); }
}
void WebPlaySynthAudioProcessor::handleMidiMessage(const juce::MidiMessage& m,const Parameters& p)
{
    if(m.isNoteOn()) startNote(m.getNoteNumber(),m.getFloatVelocity(),p);
    else if(m.isNoteOff()) stopNote(m.getNoteNumber());
    else if(m.isAllNotesOff()||m.isAllSoundOff()) for(auto& v:voices) v.stop();
}
WebPlaySynthAudioProcessor::Voice* WebPlaySynthAudioProcessor::findVoiceToStart()
{
    for(auto& v:voices) if(!v.active) return &v;
    auto* oldest=&voices.front(); for(auto& v:voices) if(v.age<oldest->age) oldest=&v; return oldest;
}
void WebPlaySynthAudioProcessor::startNote(int n,float vel,const Parameters& p)
{
    auto* v=findVoiceToStart(); v->age=++voiceAge; v->start(n,vel,p,lastPlayedFrequency); lastPlayedFrequency=juce::MidiMessage::getMidiNoteInHertz(n);
}
void WebPlaySynthAudioProcessor::stopNote(int n) { for(auto& v:voices) if(v.active&&v.note==n) v.stop(); }
float WebPlaySynthAudioProcessor::polyBlep(double t,double dt)
{
    if(dt<=0) return 0; if(t<dt) { const auto x=t/dt; return static_cast<float>(x+x-x*x-1); }
    if(t>1-dt) { const auto x=(t-1)/dt; return static_cast<float>(x*x+x+x+1); } return 0;
}
float WebPlaySynthAudioProcessor::waveformSample(double phase,int wave)
{
    switch(wave) {
        case 0:return static_cast<float>(std::sin(phase*juce::MathConstants<double>::twoPi));
        case 1:return static_cast<float>(1.0-4.0*std::abs(phase-.5));
        case 2:return static_cast<float>(2.0*phase-1.0);
        case 3:return phase<.5?1.0f:-1.0f;
        default:return 0;
    }
}
float WebPlaySynthAudioProcessor::oscillatorSample(double phase,double inc,int wave)
{
    auto s=waveformSample(phase,wave);
    if(wave==2) s-=polyBlep(phase,inc);
    else if(wave==3) { s+=polyBlep(phase,inc); auto shifted=phase+.5; if(shifted>=1) shifted-=1; s-=polyBlep(shifted,inc); }
    return s;
}
juce::AudioProcessorEditor* WebPlaySynthAudioProcessor::createEditor() { return new WebPlaySynthAudioProcessorEditor(*this); }
void WebPlaySynthAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if(auto xml=parameters.copyState().createXml()) copyXmlToBinary(*xml,dest);
}
void WebPlaySynthAudioProcessor::setStateInformation(const void* data,int size)
{
    if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WebPlaySynthAudioProcessor(); }
