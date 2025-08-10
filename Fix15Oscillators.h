#pragma once

#include "Fix15.h"
#include <cmath>

namespace fixOscs::oscillator
{
    struct Phase
    {
        void resetPhase() { phase = 0; }
        void setSampleRate(float sampleRate);
        void setFrequency(fix15 frequency);

        uint32_t next();
        uint32_t getCurrentPhase() const { return phase; }

        uint32_t phase = 0;
        uint32_t increment = 0;
        fix15 freqToIncrementFactor = 0;
    };

    struct Saw
    {
        void resetPhase() { phase.resetPhase(); }
        void setSampleRate(float sampleRate) { phase.setSampleRate(sampleRate); }
        void setFrequency(fix15 frequency) { phase.setFrequency(frequency); }

        fix15 getSample();

    private:
        Phase phase;
    };

    struct Pulse
    {
        void resetPhase() { phase.resetPhase(); }
        void setSampleRate(float sampleRate) { phase.setSampleRate(sampleRate); }
        void setFrequency(fix15 frequency) { phase.setFrequency(frequency); }
        void setPulseWidth(fix15 width) { pulseWidth = width; }

        fix15 getSample();

    private:
        Phase phase;
        fix15 pulseWidth = FIX15_HALF;  // Default 50% duty cycle
    };

    struct Sub
    {
        void resetPhase() { phase.resetPhase(); }
        void setSampleRate(float sampleRate) { phase.setSampleRate(sampleRate); }
        void setFrequency(fix15 frequency) {
            // Sub oscillator runs at half frequency (one octave down)
            fix15 halfFreq = frequency >> 1;  // Divide by 2
            phase.setFrequency(halfFreq);
        }

        fix15 getSample();

    private:
        Phase phase;
    };

    struct Noise
    {
        fix15 getSample();

    private:
        uint32_t seed = 1;
    };

    inline void Phase::setSampleRate(float sampleRate)
    {
        double increment_per_hz = 4294967296.0 / sampleRate;
        double factorDouble = increment_per_hz / 32768.0;
        freqToIncrementFactor = float2fix15(factorDouble);
    }

    inline void Phase::setFrequency(fix15 frequency)
    {
        fix15 result = multfix15(frequency, freqToIncrementFactor);
        increment = (uint32_t)result;
    }

    inline uint32_t Phase::next()
    {
        uint32_t currentPhase = phase;
        phase += increment;
        return currentPhase;
    }

    inline fix15 Saw::getSample()
    {
        uint32_t p = phase.next();
        return (fix15)(int16_t)(p >> 16);
    }

    inline fix15 Pulse::getSample()
    {
        uint32_t p = phase.next();

        // Convert pulseWidth (fix15 0-1) to uint32_t threshold
        // pulseWidth ranges 0 to FIX15_ONE (32768)
        // We need threshold in full uint32_t range (0 to UINT32_MAX)
        // Multiply by 131072 to scale: 32768 * 131072 = UINT32_MAX
        uint32_t threshold = (uint32_t)((uint64_t)pulseWidth << 17);

        // Return +1 when phase < threshold, -1 otherwise
        return (p < threshold) ? FIX15_ONE : -FIX15_ONE;
    }

    inline fix15 Sub::getSample()
    {
        uint32_t p = phase.next();

        // Sub oscillator is typically a square wave (50% duty cycle)
        // Return +1 for first half, -1 for second half
        return (p & 0x80000000) ? -FIX15_ONE : FIX15_ONE;
    }

    inline fix15 Noise::getSample()
    {
        // Linear Congruential Generator for white noise
        // Using standard constants: a=1664525, c=1013904223
        seed = seed * 1664525U + 1013904223U;

        // Convert upper 16 bits to fix15 range (-1 to +1)
        // Upper bits have better randomness than lower bits in LCG
        int16_t noise_sample = (int16_t)(seed >> 16);

        // Direct cast - int16_t and fix15 have same bit pattern
        return (fix15)noise_sample;
    }

} // namespace fixOscs::oscillator