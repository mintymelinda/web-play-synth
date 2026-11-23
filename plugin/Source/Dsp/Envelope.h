#pragma once

namespace webplaysynth::dsp {

// Sample-for-sample compatible with JUCE 8.0.6 juce::ADSR.
// Keep the stage transitions and float arithmetic aligned with JUCE: in
// particular, noteOn does not reset the current envelope value, and noteOff
// calculates its release slope from that current value.
class Adsr {
public:
    struct Parameters {
        float attack = 0.1f;
        float decay = 0.1f;
        float sustain = 1.0f;
        float release = 0.1f;
    };

    Adsr() noexcept
    {
        recalculateRates();
    }

    void setParameters(const Parameters& newParameters) noexcept
    {
        parameters = newParameters;
        recalculateRates();
    }

    const Parameters& getParameters() const noexcept
    {
        return parameters;
    }

    bool isActive() const noexcept
    {
        return state != State::idle;
    }

    void setSampleRate(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
    }

    void reset() noexcept
    {
        envelopeValue = 0.0f;
        state = State::idle;
    }

    void noteOn() noexcept
    {
        if (attackRate > 0.0f) {
            state = State::attack;
        } else if (decayRate > 0.0f) {
            envelopeValue = 1.0f;
            state = State::decay;
        } else {
            envelopeValue = parameters.sustain;
            state = State::sustain;
        }
    }

    void noteOff() noexcept
    {
        if (state != State::idle) {
            if (parameters.release > 0.0f) {
                releaseRate = static_cast<float>(
                    envelopeValue / (parameters.release * sampleRate));
                state = State::release;
            } else {
                reset();
            }
        }
    }

    float getNextSample() noexcept
    {
        switch (state) {
            case State::idle:
                return 0.0f;

            case State::attack:
                envelopeValue += attackRate;
                if (envelopeValue >= 1.0f) {
                    envelopeValue = 1.0f;
                    goToNextState();
                }
                break;

            case State::decay:
                envelopeValue -= decayRate;
                if (envelopeValue <= parameters.sustain) {
                    envelopeValue = parameters.sustain;
                    goToNextState();
                }
                break;

            case State::sustain:
                envelopeValue = parameters.sustain;
                break;

            case State::release:
                envelopeValue -= releaseRate;
                if (envelopeValue <= 0.0f)
                    goToNextState();
                break;
        }

        return envelopeValue;
    }

private:
    enum class State { idle, attack, decay, sustain, release };

    void recalculateRates() noexcept
    {
        const auto getRate = [](float distance, float timeInSeconds, double sr) {
            return timeInSeconds > 0.0f
                ? static_cast<float>(distance / (timeInSeconds * sr))
                : -1.0f;
        };

        attackRate = getRate(1.0f, parameters.attack, sampleRate);
        decayRate = getRate(1.0f - parameters.sustain, parameters.decay, sampleRate);
        releaseRate = getRate(parameters.sustain, parameters.release, sampleRate);

        if ((state == State::attack && attackRate <= 0.0f)
            || (state == State::decay
                && (decayRate <= 0.0f || envelopeValue <= parameters.sustain))
            || (state == State::release && releaseRate <= 0.0f)) {
            goToNextState();
        }
    }

    void goToNextState() noexcept
    {
        if (state == State::attack) {
            state = decayRate > 0.0f ? State::decay : State::sustain;
            return;
        }

        if (state == State::decay) {
            state = State::sustain;
            return;
        }

        if (state == State::release)
            reset();
    }

    State state = State::idle;
    Parameters parameters;
    double sampleRate = 44100.0;
    float envelopeValue = 0.0f;
    float attackRate = 0.0f;
    float decayRate = 0.0f;
    float releaseRate = 0.0f;
};

} // namespace webplaysynth::dsp
