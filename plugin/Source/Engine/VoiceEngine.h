#pragma once

#include "Dsp/Envelope.h"
#include "Dsp/Lfo.h"
#include "Dsp/Modulation.h"
#include "Dsp/Oscillator.h"
#include "Dsp/TptFilter.h"

#include <array>

namespace webplaysynth::engine {

inline double midiNoteToFrequency(int note) noexcept
{
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

struct SynthParameters {
        int osc1Wave=2, osc2Wave=3, lfoWave=0, filterType=0, unisonVoices=0;
        float osc1Octave=0, osc1Tune=0, osc1Level=.72f, osc1PulseWidth=.5f, osc2Octave=0, osc2Tune=7, osc2Level=.38f, osc2PulseWidth=.5f;
        bool hardSync=false, glideEnabled=false, vco1Enabled=true, vco2Enabled=true, filterEnvEnabled=true, ampEnvEnabled=true;
        float crossMod=0, cutoff=1800, resonance=.8f, filterContour=.55f, filterDrive=0, aftertouchDepth=0;
        float filterAttack=.025f, filterDecay=.42f, filterSustain=.32f, filterRelease=.38f;
        float ampAttack=.015f, ampDecay=.25f, ampSustain=.78f, ampRelease=.32f;
        float lfoRate=4.5f, lfoPitchDepth=0, lfoFilterDepth=0, lfoAmpDepth=0, lfoPanDepth=0, lfoPulseDepth=0, master=.72f, glideTime=.18f;
        float unisonDetune=0, stereoSpread=0;
        float chorusRate=.8f, chorusDepth=.5f, chorusMix=0;
        float delayTime=.32f, delayFeedback=.35f, delayMix=0;
        float reverbMix=0, reverbRoom=.55f, reverbDamping=.4f;
        double glideCoefficient=1.0;
        double osc1FrequencyRatio=1.0, osc2FrequencyRatio=1.0, hardSyncRatio=1.0;
        float filterDriveGain=1.0f, unisonLayerGain=1.0f;
        std::array<double,4> unisonDetuneRatios {1.0,1.0,1.0,1.0};
        std::array<float,4> unisonPanPositions {};
        std::array<float,4> unisonLeftGains {1.0f,1.0f,1.0f,1.0f};
        std::array<float,4> unisonRightGains {1.0f,1.0f,1.0f,1.0f};
};

struct Voice {
        std::array<webplaysynth::dsp::TptFilter, 2> filters;
        webplaysynth::dsp::Adsr ampEnvelope, filterEnvelope;
        bool active=false, keyDown=false, sustainHeld=false;
        int note=-1, midiChannel=1, age=0;
        float pitchBendSemitones=0.0f, aftertouch=0.0f;
        float velocity=0;
        std::array<double, 4> phase1 {}, phase2 {};
        webplaysynth::dsp::Lfo lfoOscillator;
        double currentFrequency=440, targetFrequency=440;
        int currentFilterType=-1;
        float currentCutoff=1800.0f, currentResonance=0.8f, appliedResonance=0.8f;
        float filterSmoothingCoefficient=0.0f;
        void prepare(double sampleRate);
        void start(int midiNote, float noteVelocity, int channel, float pitchBend, float pressure, const SynthParameters& p, double previousFrequency);
        void release();
        void stop();
        void render(const SynthParameters& p, double sampleRate, float& left, float& right,
            float* oscillatorLeft = nullptr, float* oscillatorRight = nullptr);
};

} // namespace webplaysynth::engine
