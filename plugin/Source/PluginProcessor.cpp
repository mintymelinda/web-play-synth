#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
juce::StringArray waveNames() { return { "Sine", "Triangle", "Saw", "Square" }; }
}

const juce::StringArray& WebPlaySynthAudioProcessor::midiCcParameterIds()
{
    static const juce::StringArray ids {
        "osc1Wave", "osc2Wave", "osc1Octave", "osc1Tune", "osc1Level",
        "osc2Octave", "osc2Tune", "osc2Level", "hardSync", "crossMod",
        "cutoff", "resonance", "filterContour", "filterType", "filterDrive", "filterAttack", "filterDecay",
        "filterSustain", "filterRelease", "ampAttack", "ampDecay", "ampSustain",
        "ampRelease", "lfoWave", "lfoRate", "lfoPitchDepth", "lfoFilterDepth",
        "master", "unisonVoices", "unisonDetune", "stereoSpread", "delayTime", "delayFeedback", "delayMix", "reverbMix", "reverbRoom", "reverbDamping", "glideEnabled", "glideTime", "vco1Enabled", "vco2Enabled",
        "filterEnvEnabled", "ampEnvEnabled"
    };
    return ids;
}

WebPlaySynthAudioProcessor::WebPlaySynthAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Pitch Input",juce::AudioChannelSet::mono(),false)
          .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"SYNTH_STATE",createParameterLayout())
{
    for (auto& assignment : midiCcToParameter)
        assignment.store(-1);
}

WebPlaySynthAudioProcessor::ParameterState::ParameterLayout WebPlaySynthAudioProcessor::createParameterLayout()
{
    ParameterState::ParameterLayout l;
    const auto f=[&](const char* id,const char* name,float lo,float hi,float v,float step=0.0f,float centre=0.0f) {
        juce::NormalisableRange<float> range(lo,hi,step);
        if (centre > lo && centre < hi)
            range.setSkewForCentre(centre);
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,range,v));
    };
    const auto b=[&](const char* id,const char* name,bool v) { l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},name,v)); };
    const auto c=[&](const char* id,const char* name,const juce::StringArray& choices,int v) { l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,choices,v)); };
    c("osc1Wave","VCO 1 Waveform",waveNames(),2); c("osc2Wave","VCO 2 Waveform",waveNames(),3);
    f("osc1Octave","VCO 1 Octave",-2,2,0,1); f("osc1Tune","VCO 1 Tune",-12,12,0,1); f("osc1Level","VCO 1 Level",0,1,.72f);
    f("osc2Octave","VCO 2 Octave",-2,2,0,1); f("osc2Tune","VCO 2 Tune",-12,12,7,1); f("osc2Level","VCO 2 Level",0,1,.38f);
    b("hardSync","VCO 2 Hard Sync",false); f("crossMod","VCO Cross Modulation",0,800,0,.1f);
    f("cutoff","Filter Cutoff",30,18000,1800,1,1200); f("resonance","Filter Resonance",.1f,18,.8f,.01f); f("filterContour","Filter Contour",0,1,.55f);
    c("filterType","Filter Mode",{ "Low-pass", "High-pass", "Band-pass" },0); f("filterDrive","Filter Drive",0,24,0,.1f);
    f("filterAttack","Filter Envelope Attack",.005f,4,.025f,.001f); f("filterDecay","Filter Envelope Decay",.005f,5,.42f,.001f); f("filterSustain","Filter Envelope Sustain",0,1,.32f); f("filterRelease","Filter Envelope Release",.005f,6,.38f,.001f);
    f("ampAttack","Amp Envelope Attack",.005f,4,.015f,.001f); f("ampDecay","Amp Envelope Decay",.005f,5,.25f,.001f); f("ampSustain","Amp Envelope Sustain",0,1,.78f); f("ampRelease","Amp Envelope Release",.005f,6,.32f,.001f);
    c("lfoWave","LFO Waveform",waveNames(),0); f("lfoRate","LFO Rate",.1f,20,4.5f,.01f); f("lfoPitchDepth","LFO Pitch Depth",0,25,0,.01f); f("lfoFilterDepth","LFO Filter Depth",0,5000,0,1);
    f("master","Master Level",0,1,.72f);
    c("unisonVoices","Unison Voices",{ "1", "2", "4" },0); f("unisonDetune","Unison Detune",0,50,0,.1f); f("stereoSpread","Stereo Spread",0,1,0,.01f);
    f("delayTime","Delay Time",.01f,1.5f,.32f,.001f); f("delayFeedback","Delay Feedback",0,.85f,.35f,.01f); f("delayMix","Delay Mix",0,1,0,.01f);
    f("reverbMix","Reverb Mix",0,1,0,.01f); f("reverbRoom","Reverb Room Size",.1f,.95f,.55f,.01f); f("reverbDamping","Reverb Damping",0,1,.4f,.01f);
    b("glideEnabled","Global Glide",false); f("glideTime","Glide Time",.01f,2,.18f,.01f);
    b("vco1Enabled","VCO 1 Audio Route",true); b("vco2Enabled","VCO 2 Audio Route",true); b("filterEnvEnabled","Filter Envelope Route",true); b("ampEnvEnabled","Amp Envelope Route",true);
    return l;
}

void WebPlaySynthAudioProcessor::Voice::prepare(double sr,int block)
{
    for (auto& filter : filters) {
        filter.prepare({sr,static_cast<juce::uint32>(block),1});
        filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        filter.reset();
    }
    currentFilterType=0;
    ampEnvelope.setSampleRate(sr); filterEnvelope.setSampleRate(sr);
}
void WebPlaySynthAudioProcessor::Voice::start(int n,float vel,const Parameters& p,double previous)
{
    note=n; velocity=vel; keyDown=true; active=true; phase1.fill(0.0); phase2.fill(0.0); lfoPhase=0;
    for (auto& filter : filters) filter.reset();
    targetFrequency=juce::MidiMessage::getMidiNoteInHertz(n);
    currentFrequency=p.glideEnabled && previous>0 ? previous : targetFrequency;
    ampEnvelope.setParameters({p.ampAttack,p.ampDecay,p.ampSustain,p.ampRelease});
    filterEnvelope.setParameters({p.filterAttack,p.filterDecay,p.filterSustain,p.filterRelease});
    ampEnvelope.noteOn(); filterEnvelope.noteOn();
}
void WebPlaySynthAudioProcessor::Voice::stop()
{
    if(active && keyDown) { keyDown=false; ampEnvelope.noteOff(); filterEnvelope.noteOff(); }
}
void WebPlaySynthAudioProcessor::Voice::render(const Parameters& p,double sr,float& left,float& right)
{
    left=0.0f;
    right=0.0f;
    if(!active) return;
    if(!keyDown && !ampEnvelope.isActive()) { active=false; note=-1; return; }
    const auto glide=p.glideEnabled ? 1.0-std::exp(-1.0/(static_cast<double>(p.glideTime)*sr)) : 1.0;
    currentFrequency+=(targetFrequency-currentFrequency)*glide;
    if (currentFilterType != p.filterType) {
        const auto type = p.filterType == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
            : p.filterType == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                : juce::dsp::StateVariableTPTFilterType::lowpass;
        for (auto& filter : filters) filter.setType(type);
        currentFilterType = p.filterType;
    }

    const auto base1=currentFrequency*std::pow(2.0,(p.osc1Octave*12.0+p.osc1Tune)/12.0);
    const auto base2=currentFrequency*std::pow(2.0,(p.osc2Octave*12.0+p.osc2Tune)/12.0);
    const auto lfo=waveformSample(lfoPhase,p.lfoWave);
    lfoPhase+=p.lfoRate/sr;
    if(lfoPhase>=1) lfoPhase-=std::floor(lfoPhase);

    const int unisonCount = 1 << juce::jlimit(0, 2, p.unisonVoices);
    const auto layerGain = 1.0f / std::sqrt(static_cast<float>(unisonCount));
    const auto panScale = juce::MathConstants<float>::sqrt2;
    float mixLeft=0.0f, mixRight=0.0f;
    for (int layer=0; layer<unisonCount; ++layer) {
        const auto position = unisonCount == 1 ? 0.0 : -1.0 + (2.0 * layer / static_cast<double>(unisonCount - 1));
        const auto detuneRatio = std::pow(2.0, p.unisonDetune * position / 1200.0);
        const auto mod = static_cast<double>(lfo*p.lfoPitchDepth);
        const auto f1=juce::jlimit(10.0,20000.0,base1*detuneRatio+mod);
        const auto f2=juce::jlimit(10.0,20000.0,base2*detuneRatio+mod);
        const auto inc2=f2/sr;
        const auto s2=oscillatorSample(phase2[static_cast<size_t>(layer)],inc2,p.osc2Wave);
        const auto fm=juce::jlimit(10.0,20000.0,f1+s2*p.crossMod);
        const auto oldPhase=phase1[static_cast<size_t>(layer)];
        const auto increment1=fm/sr;
        const auto s1=oscillatorSample(oldPhase,increment1,p.osc1Wave);
        phase1[static_cast<size_t>(layer)]+=increment1;
        if(phase1[static_cast<size_t>(layer)]>=1) phase1[static_cast<size_t>(layer)]-=std::floor(phase1[static_cast<size_t>(layer)]);
        phase2[static_cast<size_t>(layer)]+=inc2;
        if(phase2[static_cast<size_t>(layer)]>=1) phase2[static_cast<size_t>(layer)]-=std::floor(phase2[static_cast<size_t>(layer)]);
        if(p.hardSync && phase1[static_cast<size_t>(layer)]<oldPhase) phase2[static_cast<size_t>(layer)]=0.0;

        const auto oscillatorMix=s1*p.osc1Level*(p.vco1Enabled?1.0f:0.0f)
            +s2*p.osc2Level*(p.vco2Enabled?1.0f:0.0f);
        const auto pan=static_cast<float>(position*p.stereoSpread);
        const auto angle=(pan+1.0f)*juce::MathConstants<float>::pi*0.25f;
        mixLeft+=oscillatorMix*std::cos(angle)*panScale*layerGain;
        mixRight+=oscillatorMix*std::sin(angle)*panScale*layerGain;
    }

    const auto amp=p.ampEnvEnabled?ampEnvelope.getNextSample():1.0f;
    const auto env=p.filterEnvEnabled?filterEnvelope.getNextSample():0.0f;
    const auto cutoff=juce::jlimit(30.0f,19000.0f,p.cutoff*static_cast<float>(std::pow(2.0,p.filterContour*env*3.0))+lfo*p.lfoFilterDepth);
    const auto driveGain = std::pow(10.0f, p.filterDrive / 20.0f);
    const auto drivenLeft = p.filterDrive > 0.0f ? std::tanh(mixLeft * driveGain) : mixLeft;
    const auto drivenRight = p.filterDrive > 0.0f ? std::tanh(mixRight * driveGain) : mixRight;
    for (auto& filter : filters) {
        filter.setCutoffFrequency(cutoff);
        filter.setResonance(p.resonance);
    }
    left=filters[0].processSample(0,drivenLeft)*amp*velocity;
    right=filters[1].processSample(0,drivenRight)*amp*velocity;
}

void WebPlaySynthAudioProcessor::prepareToPlay(double sr,int block)
{
    currentSampleRate=sr; lastPlayedFrequency=0;
    for(auto& v:voices) v.prepare(sr,block);
    reverb.setSampleRate(sr);
    reverb.reset();
    const auto delayBufferSize=static_cast<size_t>(std::ceil(sr*2.0))+2;
    delayBufferLeft.assign(delayBufferSize,0.0f);
    delayBufferRight.assign(delayBufferSize,0.0f);
    delayWritePosition=0;
}
void WebPlaySynthAudioProcessor::releaseResources()
{
    for(auto& v:voices) { v.active=false; v.keyDown=false; }
    reverb.reset();
    std::fill(delayBufferLeft.begin(),delayBufferLeft.end(),0.0f);
    std::fill(delayBufferRight.begin(),delayBufferRight.end(),0.0f);
    delayWritePosition=0;
}
bool WebPlaySynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    if (l.inputBuses.size() != 1 || l.outputBuses.size() != 1)
        return false;

    const auto input = l.getChannelSet(true, 0);
    const auto output = l.getMainOutputChannelSet();
    const auto inputDisabled = input == juce::AudioChannelSet::disabled();
    const auto inputMono = input == juce::AudioChannelSet::mono();
    const auto outputSupported = output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();

    return (inputDisabled || inputMono) && outputSupported;
}
WebPlaySynthAudioProcessor::Parameters WebPlaySynthAudioProcessor::readParameters() const
{
    const auto v=[this](const char* id){return parameters.getRawParameterValue(id)->load();}; Parameters p;
    p.osc1Wave=juce::roundToInt(v("osc1Wave")); p.osc2Wave=juce::roundToInt(v("osc2Wave")); p.osc1Octave=v("osc1Octave"); p.osc1Tune=v("osc1Tune"); p.osc1Level=v("osc1Level");
    p.osc2Octave=v("osc2Octave"); p.osc2Tune=v("osc2Tune"); p.osc2Level=v("osc2Level"); p.hardSync=v("hardSync")>.5f; p.crossMod=v("crossMod");
    p.cutoff=v("cutoff"); p.resonance=v("resonance"); p.filterContour=v("filterContour");
    p.filterType=juce::roundToInt(v("filterType")); p.filterDrive=v("filterDrive");
    p.filterAttack=v("filterAttack"); p.filterDecay=v("filterDecay"); p.filterSustain=v("filterSustain"); p.filterRelease=v("filterRelease");
    p.ampAttack=v("ampAttack"); p.ampDecay=v("ampDecay"); p.ampSustain=v("ampSustain"); p.ampRelease=v("ampRelease");
    p.lfoWave=juce::roundToInt(v("lfoWave")); p.lfoRate=v("lfoRate"); p.lfoPitchDepth=v("lfoPitchDepth"); p.lfoFilterDepth=v("lfoFilterDepth");
    p.master=v("master"); p.unisonVoices=juce::roundToInt(v("unisonVoices")); p.unisonDetune=v("unisonDetune"); p.stereoSpread=v("stereoSpread");
    p.delayTime=v("delayTime"); p.delayFeedback=v("delayFeedback"); p.delayMix=v("delayMix");
    p.reverbMix=v("reverbMix"); p.reverbRoom=v("reverbRoom"); p.reverbDamping=v("reverbDamping");
    p.glideEnabled=v("glideEnabled")>.5f; p.glideTime=v("glideTime");
    p.vco1Enabled=v("vco1Enabled")>.5f; p.vco2Enabled=v("vco2Enabled")>.5f; p.filterEnvEnabled=v("filterEnvEnabled")>.5f; p.ampEnvEnabled=v("ampEnvEnabled")>.5f;
    return p;
}
void WebPlaySynthAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    // Measure the optional input before touching the output bus. JUCE may map
    // input and output channels onto the same processBlock buffer memory.
    float inputRms = 0.0f;
    if (getChannelCountOfBus(true, 0) > 0) {
        auto input = getBusBuffer(b, true, 0);
        if (input.getNumChannels() > 0 && input.getNumSamples() > 0) {
            const auto* samples = input.getReadPointer(0);
            double sumSquares = 0.0;
            for (int sample = 0; sample < input.getNumSamples(); ++sample)
                sumSquares += static_cast<double>(samples[sample]) * samples[sample];
            inputRms = static_cast<float>(std::sqrt(sumSquares / input.getNumSamples()));
        }
    }
    pitchInputRms.store(inputRms, std::memory_order_relaxed);

    // Only clear/write the output bus. Input-only channels in JUCE's
    // processBlock buffer may be read-only and must never be modified.
    auto output = getBusBuffer(b, false, 0);
    output.clear();

    const auto p = readParameters();
    int pos = 0;
    for (const auto meta : midi) {
        const auto at = juce::jlimit(pos, output.getNumSamples(), meta.samplePosition);
        renderRange(output, pos, at, p);
        handleMidiMessage(meta.getMessage(), p);
        pos = at;
    }
    renderRange(output, pos, output.getNumSamples(), p);
    output.applyGain(p.master);
    processEffects(output,p);
}

void WebPlaySynthAudioProcessor::processEffects(juce::AudioBuffer<float>& output,const Parameters& p)
{
    const auto channelCount=output.getNumChannels();
    const auto sampleCount=output.getNumSamples();
    const auto bufferSize=static_cast<int>(delayBufferLeft.size());
    if (bufferSize > 2 && channelCount > 0) {
        const auto delaySamples=juce::jlimit(1,bufferSize-1,juce::roundToInt(p.delayTime*static_cast<float>(currentSampleRate)));
        const auto feedback=juce::jlimit(0.0f,.85f,p.delayFeedback);
        const auto mix=juce::jlimit(0.0f,1.0f,p.delayMix);
        const auto stereo=channelCount > 1;
        for (int sample=0; sample<sampleCount; ++sample) {
            auto readPosition=delayWritePosition-delaySamples;
            if (readPosition < 0) readPosition+=bufferSize;
            const auto delayedLeft=delayBufferLeft[static_cast<size_t>(readPosition)];
            const auto delayedRight=stereo ? delayBufferRight[static_cast<size_t>(readPosition)] : delayedLeft;
            const auto inputLeft=output.getSample(0,sample);
            if (stereo) {
                const auto inputRight=output.getSample(1,sample);
                delayBufferLeft[static_cast<size_t>(delayWritePosition)]=inputLeft+delayedRight*feedback;
                delayBufferRight[static_cast<size_t>(delayWritePosition)]=inputRight+delayedLeft*feedback;
                output.setSample(0,sample,inputLeft*(1.0f-mix)+delayedLeft*mix);
                output.setSample(1,sample,inputRight*(1.0f-mix)+delayedRight*mix);
            } else {
                delayBufferLeft[static_cast<size_t>(delayWritePosition)]=inputLeft+delayedLeft*feedback;
                output.setSample(0,sample,inputLeft*(1.0f-mix)+delayedLeft*mix);
            }
            if (++delayWritePosition >= bufferSize) delayWritePosition=0;
        }
    }

    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize=p.reverbRoom;
    reverbParameters.damping=p.reverbDamping;
    reverbParameters.wetLevel=p.reverbMix;
    reverbParameters.dryLevel=1.0f-p.reverbMix;
    reverbParameters.width=1.0f;
    reverbParameters.freezeMode=0.0f;
    const auto& current=reverb.getParameters();
    if (current.roomSize!=reverbParameters.roomSize || current.damping!=reverbParameters.damping
        || current.wetLevel!=reverbParameters.wetLevel || current.dryLevel!=reverbParameters.dryLevel
        || current.width!=reverbParameters.width || current.freezeMode!=reverbParameters.freezeMode)
        reverb.setParameters(reverbParameters);

    if (channelCount > 1)
        reverb.processStereo(output.getWritePointer(0),output.getWritePointer(1),sampleCount);
    else if (channelCount == 1)
        reverb.processMono(output.getWritePointer(0),sampleCount);
}

void WebPlaySynthAudioProcessor::renderRange(juce::AudioBuffer<float>& b,int start,int end,const Parameters& p)
{
    for(int i=start;i<end;++i) {
        float left=0.0f, right=0.0f;
        for(auto& voice:voices) {
            float voiceLeft=0.0f, voiceRight=0.0f;
            voice.render(p,currentSampleRate,voiceLeft,voiceRight);
            left+=voiceLeft;
            right+=voiceRight;
        }
        if (b.getNumChannels() == 1)
            b.addSample(0,i,(left+right)*0.5f);
        else {
            b.addSample(0,i,left);
            b.addSample(1,i,right);
        }
    }
}
void WebPlaySynthAudioProcessor::handleMidiMessage(const juce::MidiMessage& m,const Parameters& p)
{
    if (m.isController()) {
        const auto cc = m.getControllerNumber();
        const auto learnTarget = midiCcLearnTarget.exchange(-1);
        if (learnTarget >= 0 && learnTarget < midiCcParameterIds().size()) {
            // Deterministic one-to-one mapping: a CC and a parameter can each
            // participate in only one assignment.
            for (auto& assignment : midiCcToParameter)
                if (assignment.load() == learnTarget)
                    assignment.store(-1);
            midiCcToParameter[static_cast<size_t>(cc)].store(learnTarget);

            // The controller movement that completes learning should also
            // establish the parameter's initial value. This avoids requiring
            // a second physical movement before the newly learned control works.
            if (auto* parameter = parameters.getParameter(midiCcParameterIds()[learnTarget]))
                parameter->setValueNotifyingHost(static_cast<float>(m.getControllerValue()) / 127.0f);
            return;
        }

        const auto parameterIndex = midiCcToParameter[static_cast<size_t>(cc)].load();
        if (parameterIndex >= 0 && parameterIndex < midiCcParameterIds().size()) {
            if (auto* parameter = parameters.getParameter(midiCcParameterIds()[parameterIndex]))
                parameter->setValueNotifyingHost(static_cast<float>(m.getControllerValue()) / 127.0f);
        }
        return;
    }

    if(m.isNoteOn()) startNote(m.getNoteNumber(),m.getFloatVelocity(),p);
    else if(m.isNoteOff()) stopNote(m.getNoteNumber());
    else if(m.isAllNotesOff()||m.isAllSoundOff()) for(auto& v:voices) v.stop();
}

void WebPlaySynthAudioProcessor::setMidiCcLearnTarget(const juce::String& parameterId)
{
    const auto index = midiCcParameterIds().indexOf(parameterId);
    midiCcLearnTarget.store(index);
}

void WebPlaySynthAudioProcessor::clearMidiCcMapping(const juce::String& parameterId)
{
    const auto index = midiCcParameterIds().indexOf(parameterId);
    if (index < 0)
        return;

    for (auto& assignment : midiCcToParameter)
        if (assignment.load() == index)
            assignment.store(-1);

    if (midiCcLearnTarget.load() == index)
        midiCcLearnTarget.store(-1);
}

juce::String WebPlaySynthAudioProcessor::getMidiCcMappingForParameter(const juce::String& parameterId) const
{
    const auto index = midiCcParameterIds().indexOf(parameterId);
    if (index < 0)
        return {};

    for (int cc = 0; cc < static_cast<int>(midiCcToParameter.size()); ++cc)
        if (midiCcToParameter[static_cast<size_t>(cc)].load() == index)
            return juce::String(cc);

    return {};
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
    if (dt <= 0)
        return 0;
    if (t < dt) {
        const auto x = t / dt;
        return static_cast<float>(x + x - x * x - 1);
    }
    if (t > 1 - dt) {
        const auto x = (t - 1) / dt;
        return static_cast<float>(x * x + x + x + 1);
    }
    return 0;
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
    if (auto xml = parameters.copyState().createXml()) {
        for (int cc = 0; cc < static_cast<int>(midiCcToParameter.size()); ++cc) {
            const auto parameterIndex = midiCcToParameter[static_cast<size_t>(cc)].load();
            if (parameterIndex >= 0 && parameterIndex < midiCcParameterIds().size())
                xml->setAttribute("MIDI_CC_" + juce::String(cc), midiCcParameterIds()[parameterIndex]);
        }
        copyXmlToBinary(*xml, dest);
    }
}

void WebPlaySynthAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size)) {
        if (!xml->hasTagName(parameters.state.getType()))
            return;

        for (auto& assignment : midiCcToParameter)
            assignment.store(-1);
        midiCcLearnTarget.store(-1);

        // Store mappings as root attributes so they remain attached to the
        // APVTS state across JUCE XML serialization and ValueTree restoration.
        // Keep reading the earlier child-element representation for compatibility
        // with states saved by pre-attribute builds.
        for (int cc = 0; cc < static_cast<int>(midiCcToParameter.size()); ++cc) {
            const auto attribute = "MIDI_CC_" + juce::String(cc);
            if (xml->hasAttribute(attribute)) {
                const auto parameterIndex = midiCcParameterIds().indexOf(xml->getStringAttribute(attribute));
                if (parameterIndex >= 0)
                    midiCcToParameter[static_cast<size_t>(cc)].store(parameterIndex);
            }
        }

        if (auto* mappings = xml->getChildByName("MIDI_CC_MAPPINGS")) {
            for (auto* mapping = mappings->getFirstChildElement(); mapping != nullptr; mapping = mapping->getNextElement()) {
                const auto cc = mapping->getIntAttribute("cc", -1);
                const auto parameterIndex = midiCcParameterIds().indexOf(mapping->getStringAttribute("parameter"));
                if (cc >= 0 && cc < static_cast<int>(midiCcToParameter.size()) && parameterIndex >= 0
                    && midiCcToParameter[static_cast<size_t>(cc)].load() < 0)
                    midiCcToParameter[static_cast<size_t>(cc)].store(parameterIndex);
            }
        }

        parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WebPlaySynthAudioProcessor(); }
