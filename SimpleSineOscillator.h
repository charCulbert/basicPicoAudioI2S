#pragma once

#include "AudioModule.h"
#include "VCAEnvelopeModule.h" // The child it owns
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "pico/multicore.h"
#include <cmath>
#include <cmath> // For std::sin
#include "AudioModule.h" // Your abstract base class
#include "SinBasedOscillator.h"
#include "NoInterLookupOscillator.h"
#include "SoftwareInterpLookupOscillator.h"
#include "HardwareLookupOscillator.h"


class SimpleSineSynth : public AudioModule {
public:
    SimpleSineSynth(float sample_rate) : envelope(sample_rate), sampleRate(sample_rate) {
        // Find our parameters by their string ID from the global store.
        for (auto* p : g_synth_parameters) {
            if (p->getID() == "attack")      p_attack = p;
            if (p->getID() == "decay")       p_decay = p;
            if (p->getID() == "sustain")     p_sustain = p;
            if (p->getID() == "release")     p_release = p;
            if (p->getID() == "masterVol")   p_master_vol = p;
        }

        // Initialize our internal smoothers with a 10ms ramp time
        s_attack.reset(sample_rate, 0.01);
        s_decay.reset(sample_rate, 0.01);
        s_sustain.reset(sample_rate, 0.01);
        s_release.reset(sample_rate, 0.01);
        s_master_vol.reset(sample_rate, 0.01);
    }

    void process(choc::buffer::InterleavedView<float> buffer) override {
        // --- 1. Handle incoming MIDI and Parameter changes ---
        update_control_signals();

        oscillator.process(buffer);

        // --- 3. Pass the generated tone through the VCA envelope ---
        envelope.process(buffer);
    }

private:
    void update_control_signals() {
        // Handle MIDI Note On/Off from the FIFO queue
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8)  & 0xFF;

            if (command == 0x90) { // NOTE_ON
                lastVelocityVolume = data2 / 127.0f;
                envelope.noteOn();
            } else if (command == 0x80) { // NOTE_OFF
                envelope.noteOff();
            }
        }

        // Update Smoother Targets from the global Parameters
        s_attack.setTargetValue(p_attack->getValue());
        s_decay.setTargetValue(p_decay->getValue());
        s_sustain.setTargetValue(p_sustain->getValue());
        s_release.setTargetValue(p_release->getValue());
        s_master_vol.setTargetValue(p_master_vol->getValue());

        // Apply smoothed values to the child envelope
        envelope.setAttackTime(s_attack.getNextValue());
        envelope.setDecayTime(s_decay.getNextValue());
        envelope.setSustainLevel(s_sustain.getNextValue());
        envelope.setReleaseTime(s_release.getNextValue());
    }

    // Helper to convert MIDI note number to frequency
    static float midi_note_to_freq(uint8_t note) {
        return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
    }

    // Child audio module VCA
    VCAEnvelopeModule envelope;

    // Internal DSP objects
    float sampleRate;

    // Pointers to the global parameters we read from
    Parameter *p_attack, *p_decay, *p_sustain, *p_release, *p_master_vol;

    // Internal smoothers for click-free audio
    SmoothedValue<float> s_attack, s_decay, s_sustain, s_release, s_master_vol;

    float lastVelocityVolume = 1.0f;
};