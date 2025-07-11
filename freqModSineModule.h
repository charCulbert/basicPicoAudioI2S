#pragma once

#include "AudioModule.h"
#include "VCAEnvelopeModule.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "pico/multicore.h"
#include <cmath>
#include <vector> // <<< CHANGED: Include vector for our voices collection
// <<< NEW: Include the reverb module so we can use it inside this class
#include "SimpleReverbModule.h"

// <<< CHANGED: Renamed class from DuophonicFMModule to PolyphonicFMModule
class PolyphonicFMModule : public AudioModule {
private:
    // <<< NEW: A constant for the number of voices makes it easy to change.
    static constexpr int NUM_VOICES = 4;

    struct Voice {
        // DSP objects
        choc::oscillator::Sine<float> modulator;
        float carrier_phase = 0.0f;
        VCAEnvelopeModule envelope;

        // Voice state
        uint8_t midiNote = 0;
        bool isActive = false;
        float velocity = 0.0f;

        // Smoothers for this voice
        SmoothedValue<float> s_base_freq;
        SmoothedValue<float> s_velocity;

        Voice(float sample_rate) : envelope(sample_rate) {
            s_base_freq.reset(sample_rate, 0.005); // Fast freq changes
            s_velocity.reset(sample_rate, 0.005);  // Smooth velocity changes
            s_base_freq.setValue(440.0f);
            s_velocity.setValue(0.0f);
        }

        void noteOn(uint8_t note, float vel) {
            midiNote = note;
            isActive = true;
            velocity = vel;

            s_base_freq.setTargetValue(midi_note_to_freq(note));
            s_velocity.setTargetValue(vel);
            envelope.noteOn();
        }

        void noteOff() {
            if (!isActive) return;
            isActive = false;
            envelope.noteOff();
        }

        static float midi_note_to_freq(uint8_t note) {
            return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
        }
    };

public:
    PolyphonicFMModule(float sample_rate)
    : sampleRate(sample_rate),
      reverb(sample_rate) // <<< NEW: Initialize the reverb member
{
        // <<< CHANGED: Initialize the vector of voices
        voices.reserve(NUM_VOICES);
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices.emplace_back(sample_rate);
        }

        // Find our parameters by their string ID from the global store.
        for (auto* p : g_synth_parameters) {
            if (p->getID() == "modIndex")    p_mod_index = p;
            if (p->getID() == "harmonicity") p_harmonicity = p;
            if (p->getID() == "attack")      p_attack = p;
            if (p->getID() == "decay")       p_decay = p;
            if (p->getID() == "sustain")     p_sustain = p;
            if (p->getID() == "release")     p_release = p;
            if (p->getID() == "masterVol")   p_master_vol = p;
        }

        // Set ramp times for global smoothers
        s_mod_index.reset(sample_rate, 0.01);
        s_harmonicity.reset(sample_rate, 0.01);
        s_attack.reset(sample_rate, 0.01);
        s_decay.reset(sample_rate, 0.01);
        s_sustain.reset(sample_rate, 0.01);
        s_release.reset(sample_rate, 0.01);
        s_master_vol.reset(sample_rate, 0.01);

        // Initialize smoothers with current parameter values
        s_mod_index.setValue(p_mod_index->getValue());
        s_harmonicity.setValue(p_harmonicity->getValue());
        s_attack.setValue(p_attack->getValue());
        s_decay.setValue(p_decay->getValue());
        s_sustain.setValue(p_sustain->getValue());
        s_release.setValue(p_release->getValue());
        s_master_vol.setValue(p_master_vol->getValue());
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        update_control_signals();

        auto numFrames = buffer.getNumFrames();

        for (uint32_t f = 0; f < numFrames; ++f) {
            float current_mod_index = s_mod_index.getNextValue();
            float current_harmonicity = s_harmonicity.getNextValue();
            float current_master_vol = s_master_vol.getNextValue();

            float mixedSample = 0.0f;

            // <<< CHANGED: Loop through all voices and mix their output
            for (auto& voice : voices) {
                if (voice.envelope.isActive()) {
                    mixedSample += processVoice(voice, current_mod_index, current_harmonicity);
                }
            }

            // <<< CHANGED: Mix level adjusted for 4 voices to prevent clipping.
            float finalSample = mixedSample * (1.0f / NUM_VOICES) * current_master_vol;
            for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, f) = finalSample;
            }
        }
        // <<< NEW: Process the entire buffer (which now contains the dry mix)
        // through our reverb module. The reverb will mix in the wet signal.
        reverb.process(buffer);
    }

private:
    float processVoice(Voice& voice, float mod_index, float harmonicity) {
        float current_base_freq = voice.s_base_freq.getNextValue();
        float current_velocity = voice.s_velocity.getNextValue();

        voice.modulator.setFrequency(current_base_freq * harmonicity, sampleRate);
        float mod_sample = voice.modulator.getSample();

        float carrier_phase_inc = two_pi * current_base_freq / sampleRate;
        float out = sin(voice.carrier_phase + (mod_index * mod_sample));
        voice.carrier_phase += carrier_phase_inc;
        if (voice.carrier_phase >= two_pi) voice.carrier_phase -= two_pi;

        float sample = out * current_velocity;

        choc::buffer::InterleavedBuffer<float> voiceBuffer(1, 1);
        voiceBuffer.getSample(0, 0) = sample;
        auto voiceView = voiceBuffer.getView();
        voice.envelope.process(voiceView);

        return voiceBuffer.getSample(0, 0);
    }

    void update_control_signals() {
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8)  & 0xFF;

            if (command == 0x90 && data2 > 0) {
                handleNoteOn(data1, data2 / 127.0f);
            } else if (command == 0x80 || (command == 0x90 && data2 == 0)) {
                handleNoteOff(data1);
            }
        }

        s_mod_index.setTargetValue(p_mod_index->getValue());
        s_harmonicity.setTargetValue(p_harmonicity->getValue());
        s_attack.setTargetValue(p_attack->getValue());
        s_decay.setTargetValue(p_decay->getValue());
        s_sustain.setTargetValue(p_sustain->getValue());
        s_release.setTargetValue(p_release->getValue());
        s_master_vol.setTargetValue(p_master_vol->getValue());

        float attack = s_attack.getNextValue();
        float decay = s_decay.getNextValue();
        float sustain = s_sustain.getNextValue();
        float release = s_release.getNextValue();

        // <<< CHANGED: Loop to update all voice envelopes at once
        for (auto& voice : voices) {
            voice.envelope.setAttackTime(attack);
            voice.envelope.setDecayTime(decay);
            voice.envelope.setSustainLevel(sustain);
            voice.envelope.setReleaseTime(release);
        }
    }

    void handleNoteOn(uint8_t note, float velocity) {
        // <<< CHANGED: New voice allocation logic for N voices.
        // 1. Find the first available (inactive) voice.
        for (auto& voice : voices) {
            if (!voice.envelope.isActive()) {
                voice.noteOn(note, velocity);
                return; // Found a free voice, we're done.
            }
        }

        // 2. If all voices are busy, steal one using a round-robin scheme.
        // This is a simple and effective voice-stealing algorithm.
        voices[next_voice_to_steal].noteOn(note, velocity);
        next_voice_to_steal = (next_voice_to_steal + 1) % NUM_VOICES;
    }

    void handleNoteOff(uint8_t note) {
        // <<< CHANGED: Loop through all voices to find the one playing this note.
        for (auto& voice : voices) {
            if (voice.isActive && voice.midiNote == note) {
                voice.noteOff();
                return; // A note should only be active on one voice at a time.
            }
        }
    }

    // <<< CHANGED: Voice members are now a vector.
    std::vector<Voice> voices;
    // <<< NEW: Index for round-robin voice stealing.
    size_t next_voice_to_steal = 0;

    float sampleRate;
    const float two_pi = 6.283185307179586f;

    // Pointers to the global parameters
    Parameter *p_mod_index, *p_harmonicity, *p_attack, *p_decay, *p_sustain, *p_release, *p_master_vol;

    // Global smoothers for shared parameters
    SmoothedValue<float> s_mod_index, s_harmonicity, s_attack, s_decay, s_sustain, s_release, s_master_vol;

    SimpleReverbModule reverb;
};