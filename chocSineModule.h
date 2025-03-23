#include "choc/audio/choc_Oscillators.h"   // CHOC oscillator header
#include "choc/audio/choc_SampleBuffers.h"      // CHOC buffer utilities
#include "AudioModule.h"

// An AudioModule that uses CHOC's Sine oscillator.
class ChocSineModule : public AudioModule {
public:
    // frequency in Hz, sampleRate in Hz, and volume (0.0 to 1.0)
    ChocSineModule(double frequency, double frequency2, double sampleRate, double vol)
      : volume(vol)
    {
        // Set up our oscillator
        osc1.resetPhase();
        osc1.setFrequency(frequency, sampleRate);
        osc2.resetPhase();
        osc2.setFrequency(frequency2, sampleRate);
    }

    // process() is called with a CHOC interleaved float view.
    // For this simple module, we'll render our sine wave into channel 0,
    // leaving channel 1 untouched (or you can duplicate it, etc.).
    void process(choc::buffer::InterleavedView<float> output) override {
        auto size = output.getSize();
        for (uint32_t f = 0; f < size.numFrames; ++f) {
            // Get a new sample from our oscillator.
            float sample1 = static_cast<float>(osc1.getSample()) * static_cast<float>(volume);
            // Write to channel 0.
            output.getSample(0, f) += sample1;
            float sample2 = static_cast<float>(osc2.getSample()) * static_cast<float>(volume);
            output.getSample(1, f) += sample2;
        }
    }

private:
    choc::oscillator::Sine<double> osc1;  // CHOC sine oscillator (using double precision)
    choc::oscillator::Sine<double> osc2;  // CHOC sine oscillator (using double precision)

    double volume;
};
