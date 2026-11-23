#pragma once

#include "Oscillator.h"

namespace webplaysynth::dsp {

// Stateful LFO with the native voice's existing sample-before-advance phase
// convention. Keep the phase update arithmetic aligned with the rendered
// reference tests: waveform is sampled at the current phase, then rate /
// sampleRate is accumulated and wrapped into [0, 1).
class Lfo {
public:
    float nextSample(float rate, double sampleRate, int waveform) noexcept
    {
        const auto sample = waveformSample(phase, waveform);
        phase += static_cast<double>(rate) / sampleRate;
        if (phase >= 1.0)
            phase -= std::floor(phase);
        return sample;
    }

    void reset() noexcept
    {
        phase = 0.0;
    }

    double getPhase() const noexcept
    {
        return phase;
    }

private:
    double phase = 0.0;
};

} // namespace webplaysynth::dsp
