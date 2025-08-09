#pragma once

#include "Fix15.h"
#include <cmath>

/**
 * Fixed-point oscillators using 16.15 format for Raspberry Pi Pico audio synthesis.
 * Similar interface to choc::oscillator but optimized for embedded fixed-point arithmetic.
 */
namespace fixOscs::oscillator
{

/// Phase accumulator for fix15 oscillators
struct Phase
{
    void resetPhase() noexcept          { phase = FIX15_ZERO; }
    void setFrequency(float frequency, float sampleRate, fix15 wrapLimit);
    
    /// Returns the current phase before incrementing it
    fix15 next(fix15 wrapLimit) noexcept;
    
    /// Get current phase without advancing (for sub-oscillator sync)
    fix15 getCurrentPhase() const noexcept { return phase; }
    
    fix15 phase = FIX15_ZERO;
    fix15 increment = FIX15_ZERO;
};

//==============================================================================
/// High-quality sine wave generator using lookup table with linear interpolation
struct Sine
{
    using SampleType = fix15;
    
    static constexpr int TABLE_SIZE = 1024;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    
    Sine();
    
    void resetPhase() noexcept                                    { phase.resetPhase(); }
    void setFrequency(float frequency, float sampleRate)         { phase.setFrequency(frequency, sampleRate, tableWrapLimit); }
    
    /// Returns the next sample with linear interpolation
    SampleType getSample() noexcept;
    
private:
    void generateLookupTable();
    
    Phase phase;
    fix15 sine_table[TABLE_SIZE];
    static constexpr fix15 tableWrapLimit = int2fix15(TABLE_SIZE);
};

//==============================================================================
/// Simple sawtooth wave generator
struct Saw
{
    using SampleType = fix15;
    
    void resetPhase() noexcept                                    { phase.resetPhase(); }
    void setFrequency(float frequency, float sampleRate)         { phase.setFrequency(frequency, sampleRate, FIX15_ONE); }
    
    /// Returns the next sample
    SampleType getSample() noexcept;
    
private:
    Phase phase;
};

//==============================================================================
/// Simple square wave generator
struct Square
{
    using SampleType = fix15;
    
    void resetPhase() noexcept                                    { phase.resetPhase(); }
    void setFrequency(float frequency, float sampleRate)         { phase.setFrequency(frequency, sampleRate, FIX15_ONE); }
    void setFrequency(float frequency, float sampleRate, float multiplier) { phase.setFrequency(frequency * multiplier, sampleRate, FIX15_ONE); }
    
    /// Returns the next sample
    SampleType getSample() noexcept;
    
    Phase phase;
};

//==============================================================================
/// Variable pulse width oscillator (like SH-101)
struct Pulse
{
    using SampleType = fix15;
    
    Pulse() : pulseWidth(FIX15_HALF) {}  // Initialize to 50% duty cycle
    
    void resetPhase() noexcept                                    { phase.resetPhase(); }
    void setFrequency(float frequency, float sampleRate)         { phase.setFrequency(frequency, sampleRate, FIX15_ONE); }
    void setPulseWidth(fix15 width) noexcept                     { pulseWidth = width; }  // 0-1 range
    
    /// Returns the next pulse sample
    SampleType getSample() noexcept;
    
    /// Returns sub-octave (half frequency) square wave locked to pulse phase
    SampleType getSubSample() noexcept;
    
private:
    Phase phase;
    fix15 pulseWidth;  // Duty cycle in fix15 format (0 to 1)
};

//==============================================================================
/// White noise generator using linear congruential generator (LCG)
struct Noise
{
    using SampleType = fix15;
    
    Noise() : seed(1) {}  // Initialize with non-zero seed
    
    void resetPhase() noexcept                                    { seed = 1; }  // Reset noise sequence
    void setFrequency(float, float) noexcept                     { /* Noise doesn't use frequency */ }
    
    /// Returns the next noise sample
    SampleType getSample() noexcept;
    
private:
    uint32_t seed;
};

//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

inline void Phase::setFrequency(float frequency, float sampleRate, fix15 wrapLimit)
{
    // Calculate increment as a fraction of the wrap limit per sample
    float incrementFloat = (frequency * fix152float(wrapLimit)) / sampleRate;
    increment = float2fix15(incrementFloat);
}

inline fix15 Phase::next(fix15 wrapLimit) noexcept
{
    fix15 currentPhase = phase;
    phase += increment;
    
    // Wrap phase when it exceeds the limit
    if (phase >= wrapLimit)
        phase -= wrapLimit;
        
    return currentPhase;
}

//==============================================================================

inline Sine::Sine()
{
    generateLookupTable();
}

inline void Sine::generateLookupTable()
{
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        double angle = (2.0 * M_PI * i) / TABLE_SIZE;
        double sine_val = sin(angle);
        sine_table[i] = float2fix15(sine_val);
    }
}

inline fix15 Sine::getSample() noexcept
{
    fix15 currentPhase = phase.next(tableWrapLimit);
    
    // Extract integer and fractional parts for interpolation
    int table_index = fix152int(currentPhase) & TABLE_MASK;
    int next_index = (table_index + 1) & TABLE_MASK;
    
    // Get fractional part (remaining bits after integer extraction)
    fix15 frac = currentPhase & 0x7FFF; // Lower 15 bits = fractional part
    
    // Get two adjacent table values
    fix15 sample0 = sine_table[table_index];
    fix15 sample1 = sine_table[next_index];
    
    // Linear interpolation: sample0 + frac * (sample1 - sample0)
    fix15 diff = sample1 - sample0;
    fix15 interpolated_diff = multfix15(frac, diff);
    
    return sample0 + interpolated_diff;
}

//==============================================================================

inline fix15 Saw::getSample() noexcept
{
    fix15 p = phase.next(FIX15_ONE);
    // Convert 0->1 range to -1->1 range: 2*p - 1
    return (p << 1) - FIX15_ONE;
}

//==============================================================================

inline fix15 Square::getSample() noexcept
{
    fix15 p = phase.next(FIX15_ONE);
    // Return +1 for first half, -1 for second half (matches inverted pulse polarity)
    return (p < FIX15_HALF) ? FIX15_ONE : -FIX15_ONE;
}

//==============================================================================

inline fix15 Pulse::getSample() noexcept
{
    fix15 p = phase.next(FIX15_ONE);
    // Return -1 when phase < pulseWidth, +1 otherwise (inverted phase to avoid cancellation with saw)
    return (p < pulseWidth) ? -FIX15_ONE : FIX15_ONE;
}

inline fix15 Pulse::getSubSample() noexcept
{
    // Create perfect sub-octave from current pulse phase (no phase advancement needed)
    // Use upper bit of phase for octave-down square wave
    fix15 current_phase = phase.getCurrentPhase();
    // Check bit 14 (second-highest bit) for octave-down square wave
    // This creates a square wave that changes every 2 pulse cycles
    return (current_phase & 0x4000) ? -FIX15_ONE : FIX15_ONE;  // Bit 14 = octave down
}

//==============================================================================

inline fix15 Noise::getSample() noexcept
{
    // Linear Congruential Generator: next = (a * seed + c) mod m
    // Using values from Numerical Recipes: a=1664525, c=1013904223, m=2^32
    seed = seed * 1664525U + 1013904223U;
    
    // Convert upper 16 bits to fix15 range (-1 to +1)
    // Take top 16 bits, convert to signed, then to fix15
    int16_t noise_sample = (int16_t)(seed >> 16);
    
    // Convert from int16 range (-32768 to +32767) to fix15 range (-1 to +1)
    return (fix15)(noise_sample);  // Already in the right range for fix15
}

} // namespace fixOscs::oscillator