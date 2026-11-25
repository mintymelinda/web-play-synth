#include "VoiceEngine.h"

#include <algorithm>
#include <cmath>

namespace webplaysynth::engine {

void Voice::prepare(double sr)
{
    for (auto& filter : filters) {
        filter.prepare(sr);
        filter.setType(webplaysynth::dsp::TptFilterType::lowpass);
        filter.reset();
    }
    currentFilterType=0;
    filterSmoothingCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sr)));
    ampEnvelope.setSampleRate(sr); filterEnvelope.setSampleRate(sr);
}
void Voice::start(
    int n, float vel, int channel, float pitchBend, float pressure, const SynthParameters& p, double previous)
{
    note=n;
    velocity=vel;
    midiChannel=std::clamp(channel, 1, 16);
    pitchBendSemitones=pitchBend;
    aftertouch=pressure;
    keyDown=true;
    sustainHeld=false;
    active=true;
    phase1.fill(0.0);
    phase2.fill(0.0);
    lfoOscillator.reset();
    for (auto& filter : filters) filter.reset();
    targetFrequency=midiNoteToFrequency(n)
        * std::pow(2.0, static_cast<double>(pitchBendSemitones) / 12.0);
    currentFrequency=p.glideEnabled && previous>0 ? previous : targetFrequency;
    currentCutoff=p.cutoff;
    currentResonance=p.resonance;
    appliedResonance=p.resonance;
    for (auto& filter : filters)
        filter.setResonance(p.resonance);
    ampEnvelope.setParameters({p.ampAttack,p.ampDecay,p.ampSustain,p.ampRelease});
    filterEnvelope.setParameters({p.filterAttack,p.filterDecay,p.filterSustain,p.filterRelease});
    ampEnvelope.noteOn();
    filterEnvelope.noteOn();
}
void Voice::release()
{
    if (active && !keyDown && sustainHeld) {
        sustainHeld=false;
        ampEnvelope.noteOff();
        filterEnvelope.noteOff();
    } else if (active && !keyDown && ampEnvelope.isActive()) {
        ampEnvelope.noteOff();
        filterEnvelope.noteOff();
    }
}
void Voice::stop()
{
    if (active) {
        keyDown=false;
        sustainHeld=false;
        ampEnvelope.noteOff();
        filterEnvelope.noteOff();
    }
}
void Voice::render(const SynthParameters& p,double sr,float& left,float& right,
    float* oscillatorLeft,float* oscillatorRight)
{
    left=0.0f;
    right=0.0f;
    if(!active) return;
    if(!keyDown && !ampEnvelope.isActive()) { active=false; note=-1; return; }
    // Match the browser engine's exponential ramp: reach 99.9% of the target within the configured time.
    const auto glide=p.glideCoefficient;
    currentFrequency+=(targetFrequency-currentFrequency)*glide;
    if (currentFilterType != p.filterType) {
        const auto type = p.filterType == 1 ? webplaysynth::dsp::TptFilterType::highpass
            : p.filterType == 2 ? webplaysynth::dsp::TptFilterType::bandpass
                                : webplaysynth::dsp::TptFilterType::lowpass;
        for (auto& filter : filters) filter.setType(type);
        currentFilterType = p.filterType;
    }

    const auto base1=currentFrequency*p.osc1FrequencyRatio;
    const auto base2=currentFrequency*p.osc2FrequencyRatio;
    const auto lfo = lfoOscillator.nextSample(p.lfoRate, sr, p.lfoWave);

    const int unisonCount = 1 << std::clamp(p.unisonVoices, 0, 2);
    float mixLeft=0.0f, mixRight=0.0f;
    for (int layer=0; layer<unisonCount; ++layer) {
        const auto detuneRatio = p.unisonDetuneRatios[static_cast<size_t>(layer)];
        const auto f1=std::clamp(webplaysynth::dsp::modulateFrequency(base1,detuneRatio,lfo,p.lfoPitchDepth), 10.0, 20000.0);
        const auto f2=std::clamp(webplaysynth::dsp::modulateFrequency(base2,detuneRatio,lfo,p.lfoPitchDepth), 10.0, 20000.0);
        const auto inc2=f2/sr;
        float s2=0.0f;
        if (p.hardSync) {
            // The browser engine builds a saw-shaped PeriodicWave from the
            // oscillator frequency ratio, overriding VCO 2's selected waveform.
            const auto syncRatio=p.hardSyncRatio;
            const auto syncMasterFrequency=std::clamp(f1, 10.0, 20000.0);
            const auto syncPhase=phase2[static_cast<size_t>(layer)]*syncRatio;
            const auto wrappedSyncPhase=syncPhase-std::floor(syncPhase);
            const auto syncIncrement=std::clamp(syncRatio*syncMasterFrequency/sr, 0.0001, 0.49);
            s2=webplaysynth::dsp::oscillatorSample(wrappedSyncPhase,syncIncrement,2);
            const auto masterResetFraction=syncRatio-std::floor(syncRatio);
            const auto masterIncrement=std::clamp(syncMasterFrequency/sr, 0.0001, 0.49);
            s2-=static_cast<float>(masterResetFraction)
                * webplaysynth::dsp::polyBlep(phase2[static_cast<size_t>(layer)],masterIncrement);
        } else {
            s2=webplaysynth::dsp::oscillatorSample(phase2[static_cast<size_t>(layer)],inc2,p.osc2Wave,
                webplaysynth::dsp::modulatePulseWidth(p.osc2PulseWidth,lfo,p.lfoPulseDepth));
        }
        const auto fm=std::clamp(f1+s2*p.crossMod, 10.0, 20000.0);
        const auto oldPhase=phase1[static_cast<size_t>(layer)];
        const auto increment1=fm/sr;
        const auto s1=webplaysynth::dsp::oscillatorSample(oldPhase,increment1,p.osc1Wave,
            webplaysynth::dsp::modulatePulseWidth(p.osc1PulseWidth,lfo,p.lfoPulseDepth));
        phase1[static_cast<size_t>(layer)]+=increment1;
        if(phase1[static_cast<size_t>(layer)]>=1) phase1[static_cast<size_t>(layer)]-=std::floor(phase1[static_cast<size_t>(layer)]);
        const auto phase2Increment = p.hardSync
            ? f1/sr
            : inc2;
        phase2[static_cast<size_t>(layer)]+=phase2Increment;
        if(phase2[static_cast<size_t>(layer)]>=1) phase2[static_cast<size_t>(layer)]-=std::floor(phase2[static_cast<size_t>(layer)]);

        const auto oscillatorMix=s1*p.osc1Level*(p.vco1Enabled?1.0f:0.0f)
            +s2*p.osc2Level*(p.vco2Enabled?1.0f:0.0f);
        if (p.lfoPanDepth > 0.0f) {
            const auto pan=webplaysynth::dsp::modulatePan(p.unisonPanPositions[static_cast<size_t>(layer)],lfo,p.lfoPanDepth);
            const auto angle=(pan+1.0f)*3.14159265358979323846f*0.25f;
            const auto panScale=1.4142135623730950488f;
            mixLeft+=oscillatorMix*std::cos(angle)*panScale*p.unisonLayerGain;
            mixRight+=oscillatorMix*std::sin(angle)*panScale*p.unisonLayerGain;
        } else {
            mixLeft+=oscillatorMix*p.unisonLeftGains[static_cast<size_t>(layer)];
            mixRight+=oscillatorMix*p.unisonRightGains[static_cast<size_t>(layer)];
        }
    }

    if (oscillatorLeft != nullptr)
        *oscillatorLeft = mixLeft;
    if (oscillatorRight != nullptr)
        *oscillatorRight = mixRight;

    // Advance both envelopes regardless of routing. Bypass changes only the
    // signal contribution; envelope state must keep progressing so note-off
    // can retire voices and re-enabling a route cannot resume stale state.
    const auto ampEnvelopeValue=ampEnvelope.getNextSample();
    const auto filterEnvelopeValue=filterEnvelope.getNextSample();
    const auto amp=p.ampEnvEnabled?ampEnvelopeValue:1.0f;
    const auto env=p.filterEnvEnabled?filterEnvelopeValue:0.0f;
    // Match Web Audio AudioParam.setTargetAtTime(..., 0.015): native
    // filter controls must not jump discontinuously between audio blocks.
    currentCutoff += (p.cutoff - currentCutoff) * filterSmoothingCoefficient;
    currentResonance += (p.resonance - currentResonance) * filterSmoothingCoefficient;
    const auto cutoff=webplaysynth::dsp::modulateFilterCutoff(
        currentCutoff,env,p.filterContour,lfo,p.lfoFilterDepth);
    const auto drivenLeft = p.filterDrive > 0.0f ? std::tanh(mixLeft * p.filterDriveGain) : mixLeft;
    const auto drivenRight = p.filterDrive > 0.0f ? std::tanh(mixRight * p.filterDriveGain) : mixRight;
    if (std::abs(currentResonance - appliedResonance) >= 0.01f) {
        for (auto& filter : filters)
            filter.setResonance(currentResonance);
        appliedResonance = currentResonance;
    }
    for (auto& filter : filters)
        filter.setCutoffFrequency(cutoff);
    const auto ampLfo=webplaysynth::dsp::modulateAmplitude(lfo,p.lfoAmpDepth);
    const auto pressureGain=1.0f+std::clamp(aftertouch, 0.0f, 1.0f)*p.aftertouchDepth;
    // JUCE's TPT band-pass output is Q-scaled, unlike Web Audio's
    // normalized BiquadFilterNode band-pass response. Compensate by R2
    // (1 / Q) so resonance changes bandwidth without adding output gain.
    const auto bandpassGain = p.filterType == 2 ? 1.0f / currentResonance : 1.0f;
    left=filters[0].processSample(drivenLeft)*bandpassGain*amp*ampLfo*velocity*pressureGain;
    right=filters[1].processSample(drivenRight)*bandpassGain*amp*ampLfo*velocity*pressureGain;
}

} // namespace webplaysynth::engine
