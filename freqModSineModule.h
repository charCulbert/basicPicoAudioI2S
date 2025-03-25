#ifndef FREQMODSINEMODULE_H
#define FREQMODSINEMODULE_H

#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"
#include "pico/multicore.h"

enum Command {
    CMD_VOL_DOWN = 1,
    CMD_VOL_UP = 2,
    CMD_MOD_DOWN = 3,
    CMD_MOD_UP = 4,
    CMD_HARM_DOWN = 5,
    CMD_HARM_UP = 6
};


class FreqModSineModule : public AudioModule {
public:
    FreqModSineModule(double frequency, double harmonicityRatio, double modulationIndex, double sampleRate, double vol)
        : baseFrequency(frequency),
          modulationIndex(modulationIndex),
          sampleRate(sampleRate),
          volume(vol),
          harmonicityRatio(harmonicityRatio),
          currentFreq(frequency)  // initialize currentFreq
    {
        osc.resetPhase();
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    // Process() renders the modulated sine wave to both channels.
    void process(choc::buffer::InterleavedView<float> output) override {
        // Temporary FIFO clearing loop for testing.

        while (multicore_fifo_rvalid()) {
            int CMD = multicore_fifo_pop_blocking();
            if (CMD == CMD_VOL_DOWN) {
                if (volume >= 0.01f) {
                    volume -= 0.01f;
                    setVolume(volume);
                }
            }
            else if (CMD == CMD_VOL_UP) {
                if (volume <= 0.5f) {
                    volume += 0.01f;
                    setVolume(volume);
                }
            }
            else if (CMD == CMD_MOD_DOWN) {
                if (modulationIndex > 1.0f) {
                    modulationIndex -= 0.1f;
                    setModulationIndex(modulationIndex);
                }
            }
            else if (CMD == CMD_MOD_UP) {
                modulationIndex += 0.1f;
                setModulationIndex(modulationIndex);
            }
            else if (CMD == CMD_HARM_DOWN) {
                if (harmonicityRatio > 1.0f) {
                    harmonicityRatio -= 0.1f;
                    setHarmonicityRatio(harmonicityRatio);
                }
            }
            else if (CMD == CMD_HARM_UP) {
                harmonicityRatio += 0.1f;
                setHarmonicityRatio(harmonicityRatio);
            }
        }

            auto size = output.getSize();
            const double twoPi = 6.283185307179586;
            double phaseInc = twoPi * baseFrequency / sampleRate;
            for (uint32_t f = 0; f < size.numFrames; ++f) {
                // Get the modulator value (from osc, which handles its own phase)
                double mod = osc.getSample();  // output between -1 and 1

                // Compute FM synthesis output directly.
                double out = sin(carrierPhase + modulationIndex * mod);

                // Increment the carrier phase manually
                carrierPhase += phaseInc;
                if (carrierPhase >= twoPi)
                    carrierPhase -= twoPi;

                // Write the sample (scaled by volume) to both channels.
                float sample1 = static_cast<float>(out * volume);
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
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    void setBaseFrequency(double newFreq) {
        baseFrequency = newFreq;
        osc.setFrequency(baseFrequency * harmonicityRatio, sampleRate);
    }

    // In FreqModSineModule.h, inside the class public section:
    void setVolume(double newVolume) {
        volume = newVolume;
    }

private:

    choc::oscillator::Sine<double> osc;  // Modulator oscillator.
    double currentFreq;  // Initialize this to baseFrequency in the constructor
    double volume;
    double modulationIndex;
    double baseFrequency;
    double sampleRate;
    double harmonicityRatio;
    double resultingFreq;
    const double twoPi = 6.283185307179586;
    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;
};

#endif // FREQMODSINEMODULE_H
