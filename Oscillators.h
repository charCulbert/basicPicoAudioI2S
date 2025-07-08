#pragma once

#include <array>
#include <cmath>
#include "AudioModule.h"       // Your abstract base class
#include "hardware/interp.h" // For the hardware interpolator

// --- UNIFIED COMMON DEFINITIONS ---
static constexpr int TABLE_SIZE = 2048; // Using 2048 for best quality

// The float table for the software-based oscillators
static constexpr auto sineTable_f32 = [] {
    std::array<float, TABLE_SIZE> table{};
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i] = std::sin(2.0f * M_PI * (float)i / (float)TABLE_SIZE);
    }
    return table;
}();

// The int16_t table for the hardware-based oscillator.
// The hardware will generate pointers directly into this table.
static constexpr auto sineTable_i16 = [] {
    std::array<int16_t, TABLE_SIZE> table{};
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i] = 32767.0f * std::sin(2.0f * M_PI * (float)i / (float)TABLE_SIZE);
    }
    return table;
}();


//==============================================================================
// METHOD 1: Direct Calculation using sinf() - "The Slow Baseline"
//==============================================================================
class SlowMathOscillator : public AudioModule {
public:
    float gain = 1.0f;
    void setFrequency(float freq, float sampleRate);
    void process(choc::buffer::InterleavedView<float>& buffer) override;
private:
    float phase = 0.0f, phase_increment = 0.0f;
};
void SlowMathOscillator::setFrequency(float freq, float sampleRate) { phase_increment = (2.0f * M_PI * freq) / sampleRate; }
void SlowMathOscillator::process(choc::buffer::InterleavedView<float>& buffer) {
    for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
        float output = std::sin(phase);
        output *= gain;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { buffer.getSample(ch, frame) += output; }
        phase += phase_increment;
        if (phase >= 2.0f * M_PI) { phase -= 2.0f * M_PI; }
    }
}


//==============================================================================
// METHOD 2: Lookup Table with No Interpolation - "Fast but Low Quality"
//==============================================================================
class FastLutOscillator : public AudioModule {
public:
    float gain = 1.0f;
    void setFrequency(float freq, float sampleRate) { phase_increment = (float)TABLE_SIZE * freq / sampleRate; }
    void process(choc::buffer::InterleavedView<float>& buffer) override {
        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            float output = sineTable_f32[(int)phase];
            output *= gain;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { buffer.getSample(ch, frame) += output; }
            phase += phase_increment;
            if (phase >= TABLE_SIZE) { phase -= TABLE_SIZE; }
        }
    }
private:
    float phase = 0.0f, phase_increment = 0.0f;
};


//==============================================================================
// METHOD 3: Lookup Table with Software Interpolation - "Slower but High Quality"
//==============================================================================
class SoftwareInterpOscillator : public AudioModule {
public:
    float gain = 1.0f;
    void setFrequency(float freq, float sampleRate) { phase_increment = (float)TABLE_SIZE * freq / sampleRate; }
    void process(choc::buffer::InterleavedView<float>& buffer) override {
        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            int index = (int)phase;
            float fraction = phase - index;
            float val1 = sineTable_f32[index];
            float val2 = sineTable_f32[(index + 1) % TABLE_SIZE];
            float output = val1 + (val2 - val1) * fraction;
            output *= gain;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { buffer.getSample(ch, frame) += output; }
            phase += phase_increment;
            if (phase >= TABLE_SIZE) { phase -= TABLE_SIZE; }
        }
    }
private:
    float phase = 0.0f, phase_increment = 0.0f;
};


//==============================================================================
// METHOD 4: Lookup Table with Pico Hardware "Interpolator" as Address Generator
//==============================================================================
class HardwareAddressOscillator : public AudioModule {
public:
    float gain = 1.0f;

    // The constructor handles the one-time setup of the hardware.
    HardwareAddressOscillator() {
        // IMPORTANT: This class claims interp0, lane 0 for its own use.
        // If another part of your code uses this hardware, or if you create
        // more than one instance of this class, they will conflict.

        interp_config cfg = interp_default_config();

        // Configure for fixed-point phase accumulation and table-pointer generation.
        // A shift of 15 and mask of 1..11 prepares the hardware to output a
        // byte-offset into our 2048-entry (11-bit) table of 16-bit values.
        interp_config_set_shift(&cfg, 15);
        interp_config_set_mask(&cfg, 1, 11);
        // On POP, add BASE0 to ACCUM0. This makes the phase advance automatically.
        interp_config_set_add_raw(&cfg, true);

        interp_set_config(interp0, 0, &cfg);

        // BASE2 holds the base address of our lookup table.
        interp0->base[2] = (uint32_t)sineTable_i16.data();

        // Initialize phase accumulator and step to 0 to be silent at start.
        interp0->accum[0] = 0;
        interp0->base[0] = 0;
    }

    void setFrequency(float freq, float sampleRate) {
        // Calculate the 32-bit fixed-point phase increment for the frequency.
        // Formula: step = (frequency * TABLE_SIZE * 2^16) / sampleRate
        uint32_t step = (uint32_t)((freq * (float)TABLE_SIZE * 65536.0f) / sampleRate);

        // Load the step value into the hardware. This controls the pitch.
        interp0->base[0] = step;
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            // 1. Get pointer from interpolator. Reading POP also increments phase.
            int16_t* sample_ptr = (int16_t*)interp0->pop[2];

            // 2. Dereference the pointer to get the 16-bit integer sample.
            int16_t int_sample = *sample_ptr;

            // 3. Convert the 16-bit sample to a float in the range [-1.0, 1.0].
            float float_sample = (float)int_sample / 32767.0f;

            // 4. Apply gain and add to the output buffer for all channels.
            float output = float_sample * gain;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }
        }
    }

    // Note: No private phase members are needed. The phase state is held
    // entirely within the interpolator's hardware registers.
};


//==============================================================================
// METHOD 5: TRUE HARDWARE INTERPOLATION - High Quality, High Performance
//==============================================================================
class HardwareInterpOscillator : public AudioModule {
public:
    float gain = 1.0f;

    // The constructor sets up the interpolator for blend mode.
    HardwareInterpOscillator() {
        // IMPORTANT: This class claims interp0 for its own use. It will conflict
        // with other code trying to use interp0, including the HardwareAddressOscillator.

        // As confirmed by testing and documentation:
        // 1. Set LANE 0 to BLEND mode. This makes it the "blender".
        interp_config cfg0 = interp_default_config();
        interp_config_set_blend(&cfg0, true);
        interp_set_config(interp0, 0, &cfg0);

        // 2. The SIGNED flag on LANE 1 controls the math for the blend.
        interp_config cfg1 = interp_default_config();
        interp_config_set_signed(&cfg1, true);
        interp_set_config(interp0, 1, &cfg1);
    }

    void setFrequency(float freq, float sampleRate) {
        // The phase accumulator is a 32-bit unsigned integer in software.
        // The high 11 bits are the table index, the rest is the fraction.
        // step = (frequency * 2^32) / sampleRate
        // Use 64-bit intermediate to prevent overflow for high frequencies.
        phase_increment = (uint32_t)(((uint64_t)freq * (1LL << 32)) / sampleRate);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        // Use local copies for performance inside the loop
        uint32_t current_phase = phase;
        const uint32_t phase_inc = phase_increment;

        // TABLE_SIZE_MASK is (2048 - 1) = 2047, or 0x7FF
        const int TABLE_SIZE_MASK = TABLE_SIZE - 1;
        // The number of bits for the table index (2^11 = 2048)
        const int TABLE_INDEX_BITS = 11;

        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            // 1. Calculate table index from the high bits of our software phase.
            uint32_t index = current_phase >> (32 - TABLE_INDEX_BITS);

            // 2. Load the two adjacent samples into the shared BASE registers.
            interp0->base[0] = sineTable_i16[index];
            // Use bitwise AND for fast, guaranteed wrapping.
            interp0->base[1] = sineTable_i16[(index + 1) & TABLE_SIZE_MASK];

            // 3. Extract the 8-bit fractional part and load it into ACCUM1.
            uint32_t fraction = (current_phase >> (32 - TABLE_INDEX_BITS - 8)) & 0xFF;
            interp0->accum[1] = fraction;

            // 4. Read the blended result from PEEK[1].
            int16_t int_sample = (int16_t)interp0->peek[1];

            // 5. Convert to float, apply gain, and write to buffer.
            float float_sample = (float)int_sample / 32767.0f;
            float output = float_sample * gain;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }

            // 6. Advance the software phase for the next sample.
            current_phase += phase_inc;
        }
        // Store the final phase back to the member variable for the next block.
        phase = current_phase;
    }

private:
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
};


//==============================================================================
// FIXED VERSION: Corrected Phase Calculation
//==============================================================================
class FixedPointInterpOscillator : public AudioModule {
public:
    float gain = 1.0f;

    void setFrequency(float freq, float sampleRate) {
        // Calculate phase increment for 32-bit accumulator
        phase_increment = (uint32_t)((freq * (1ULL << 32)) / sampleRate);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        uint32_t current_phase = phase;
        const uint32_t phase_inc = phase_increment;
        const float gain_val = gain;

        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            // Extract table index from high 11 bits (for 2048-entry table)
            uint32_t index = current_phase >> (32 - 11);  // Top 11 bits

            // Extract fractional part from next 16 bits
            uint32_t frac = (current_phase >> (32 - 11 - 16)) & 0xFFFF;

            // Linear interpolation using fixed-point math
            int16_t val1 = sineTable_i16[index];
            int16_t val2 = sineTable_i16[(index + 1) & (TABLE_SIZE - 1)];

            // Fixed-point interpolation, then convert to float
            int32_t interp = val1 + (((int32_t)(val2 - val1) * frac) >> 16);
            float output = ((float)interp / 32767.0f) * gain_val;

            // Write to all channels
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }

            current_phase += phase_inc;
        }
        phase = current_phase;
    }

private:
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
};

//==============================================================================
// CORRECTED TEMPLATE VERSION
//==============================================================================
template<int TABLE_SIZE_BITS = 11>
class TemplateOptimizedOscillator : public AudioModule {
public:
    static constexpr int TABLE_SIZE = 1 << TABLE_SIZE_BITS;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;

    float gain = 1.0f;

    void setFrequency(float freq, float sampleRate) {
        phase_increment = (uint32_t)((freq * (1ULL << 32)) / sampleRate);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        uint32_t current_phase = phase;
        const uint32_t phase_inc = phase_increment;
        const float gain_val = gain;

        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            // Extract table index from top TABLE_SIZE_BITS bits
            uint32_t index = current_phase >> (32 - TABLE_SIZE_BITS);

            // Extract fractional part from next 16 bits for interpolation
            uint32_t frac = (current_phase >> (32 - TABLE_SIZE_BITS - 16)) & 0xFFFF;

            int16_t val1 = sineTable_i16[index];
            int16_t val2 = sineTable_i16[(index + 1) & TABLE_MASK];

            int32_t interp = val1 + (((int32_t)(val2 - val1) * frac) >> 16);
            float output = ((float)interp / 32767.0f) * gain_val;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }

            current_phase += phase_inc;
        }
        phase = current_phase;
    }

private:
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
};

//==============================================================================
// SIMPLE DEBUGGING VERSION - Let's verify the basic math works
//==============================================================================
class DebugOscillator : public AudioModule {
public:
    float gain = 1.0f;

    void setFrequency(float freq, float sampleRate) {
        phase_increment = (uint32_t)((freq * (1ULL << 32)) / sampleRate);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        uint32_t current_phase = phase;
        const uint32_t phase_inc = phase_increment;
        const float gain_val = gain;

        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            // Simple version - just check if we're getting the right index range
            uint32_t index = current_phase >> 21;  // For 2048 table: 32-11 = 21

            // Clamp index to valid range (should not be necessary, but let's be safe)
            index = index & 0x7FF;  // 0x7FF = 2047, ensures 0-2047 range

            // No interpolation for debugging - just direct lookup
            int16_t int_sample = sineTable_i16[index];
            float output = ((float)int_sample / 32767.0f) * gain_val;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }

            current_phase += phase_inc;
        }
        phase = current_phase;
    }

private:
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
};

//==============================================================================
// QUAD OSCILLATOR - CORRECTED VERSION
//==============================================================================
class QuadOscillator : public AudioModule {
public:
    struct OscParams {
        uint32_t phase = 0;
        uint32_t phase_increment = 0;
        float gain = 1.0f;
    };

    OscParams oscs[4];

    void setOscFrequency(int osc_idx, float freq, float sampleRate) {
        if (osc_idx < 4) {
            oscs[osc_idx].phase_increment = (uint32_t)((freq * (1ULL << 32)) / sampleRate);
        }
    }

    void setOscGain(int osc_idx, float gain) {
        if (osc_idx < 4) {
            oscs[osc_idx].gain = gain;
        }
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            float mixed_output = 0.0f;

            // Process all 4 oscillators in one loop
            for (int osc = 0; osc < 4; ++osc) {
                uint32_t index = oscs[osc].phase >> 21;  // Extract top 11 bits
                uint32_t frac = (oscs[osc].phase >> 5) & 0xFFFF;  // Next 16 bits

                int16_t val1 = sineTable_i16[index];
                int16_t val2 = sineTable_i16[(index + 1) & (TABLE_SIZE - 1)];

                int32_t interp = val1 + (((int32_t)(val2 - val1) * frac) >> 16);
                mixed_output += ((float)interp / 32767.0f) * oscs[osc].gain;

                oscs[osc].phase += oscs[osc].phase_increment;
            }

            // Write mixed result to all channels
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += mixed_output;
            }
        }
    }
};

//==============================================================================
// FAST 8-BIT INTERPOLATION - CORRECTED VERSION
//==============================================================================
class FastInterpOscillator : public AudioModule {
public:
    float gain = 1.0f;

    void setFrequency(float freq, float sampleRate) {
        phase_increment = (uint32_t)((freq * (1ULL << 32)) / sampleRate);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        uint32_t current_phase = phase;
        const uint32_t phase_inc = phase_increment;
        const float gain_val = gain;

        for (int frame = 0; frame < buffer.getNumFrames(); ++frame) {
            uint32_t index = current_phase >> 21;  // Top 11 bits for table index

            // Use only 8 bits of fraction for faster interpolation
            uint32_t frac = (current_phase >> 13) & 0xFF;  // 8 bits of fraction

            int16_t val1 = sineTable_i16[index];
            int16_t val2 = sineTable_i16[(index + 1) & (TABLE_SIZE - 1)];

            // 8-bit interpolation
            int32_t interp = val1 + (((int32_t)(val2 - val1) * frac) >> 8);
            float output = ((float)interp / 32767.0f) * gain_val;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, frame) += output;
            }

            current_phase += phase_inc;
        }
        phase = current_phase;
    }

private:
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
};