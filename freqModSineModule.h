#ifndef FREQMODSINEMODULE_H
#define FREQMODSINEMODULE_H

#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"
#include "pico/multicore.h"

class FreqModSineModule : public AudioModule {
public:
    FreqModSineModule(double frequency, double harmonicityRatio, double modulationIndex, double sampleRate, double vol)
        : baseFrequency(frequency),
          modulationIndex(modulationIndex),
          sampleRate(sampleRate),
          volume(vol),
          harmonicityRatio(harmonicityRatio),
          currentFreq(frequency)
    {
        osc.resetPhase();
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    // Process() is now clean and focused only on audio generation.
    void process(choc::buffer::InterleavedView<float> output) override {
        auto size = output.getSize();
        double phaseInc = twoPi * baseFrequency / sampleRate;
        for (uint32_t f = 0; f < size.numFrames; ++f) {
            double mod = osc.getSample();
            double out = sin(carrierPhase + modulationIndex * mod);

            carrierPhase += phaseInc;
            if (carrierPhase >= twoPi)
                carrierPhase -= twoPi;

            float sample1 = static_cast<float>(out * volume);
            output.getSample(0, f) += sample1;
            output.getSample(1, f) += sample1;
        }
    }

    // --- Public Setters ---
    void setModulationIndex(double newIndex) { modulationIndex = newIndex > 0 ? newIndex : 0; }
    void setHarmonicityRatio(double newRatio) {
        harmonicityRatio = newRatio > 0 ? newRatio : 0;
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }
    void setBaseFrequency(double newFreq) {
        baseFrequency = newFreq;
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }
    void setVolume(double newVolume) { volume = newVolume; }

    // --- Public Getters (NEW) ---
    // These allow other modules to safely read the current state.
    double getModulationIndex() const { return modulationIndex; }
    double getHarmonicityRatio() const { return harmonicityRatio; }


private:
    choc::oscillator::Sine<double> osc;
    double currentFreq;
    double volume;
    double modulationIndex;
    double baseFrequency;
    double sampleRate;
    double harmonicityRatio;
    const double twoPi = 6.283185307179586;
    double carrierPhase = 0.0;
};

#endif // FREQMODSINEMODULE_H