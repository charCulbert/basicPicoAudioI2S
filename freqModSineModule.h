#ifndef FREQMODSINEMODULE_H
#define FREQMODSINEMODULE_H

#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"

class FreqModSineModule : public AudioModule {
public:
    // frequency in Hz, sampleRate in Hz, and volume (0.0 to 1.0)
    FreqModSineModule(double frequency, double harmonicityRatio, double modulationIndex, double sampleRate, double vol)
      : baseFrequency(frequency),
        modulationIndex(modulationIndex),
        sampleRate(sampleRate),
        volume(vol),
        harmonicityRatio(harmonicityRatio)
    {
        osc1.resetPhase();
        osc1.setFrequency(baseFrequency, sampleRate);
        osc2.resetPhase();
        osc2.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    // Process() renders the modulated sine wave to both channels.
    void process(choc::buffer::InterleavedView<float> output) override {
        auto size = output.getSize();
        for (uint32_t f = 0; f < size.numFrames; ++f) {
            // Get modulator sample WITHOUT scaling by volume.
            float modulator = static_cast<float>(osc2.getSample());
            // Calculate the instantaneous frequency of the carrier.
            resultingFreq = baseFrequency + modulator * modulationIndex;
            osc1.setFrequency(resultingFreq, sampleRate);

            // Get carrier sample and scale by overall volume.
            float sample1 = static_cast<float>(osc1.getSample()) * static_cast<float>(volume);

            // Write to both channels.
            output.getSample(0, f) += sample1;
            output.getSample(1, f) += sample1;
        }
    }

    // Setter methods to adjust parameters via serial.
    void setModulationIndex(double newIndex) {
        modulationIndex = newIndex;
    }

    void setHarmonicityRatio(double newRatio) {
        harmonicityRatio = newRatio;
        osc2.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    void setBaseFrequency(double newFreq) {
        baseFrequency = newFreq;
        osc1.setFrequency(baseFrequency, sampleRate);
        osc2.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    // In FreqModSineModule.h, inside the class public section:
    void setVolume(double newVolume) {
        volume = newVolume;
    }

private:
    choc::oscillator::Sine<double> osc1;  // Carrier oscillator.
    choc::oscillator::Sine<double> osc2;  // Modulator oscillator.

    double volume;
    double modulationIndex;
    double baseFrequency;
    double sampleRate;
    double harmonicityRatio;
    double resultingFreq;
};

#endif // FREQMODSINEMODULE_H
