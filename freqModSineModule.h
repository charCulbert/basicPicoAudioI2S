#pragma once

#include "AudioModule.h"
#include "VCAEnvelopeModule.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "pico/multicore.h"
#include <cmath>
#include <array>

class DuophonicFMModule : public AudioModule {
private:
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
        SmoothedValue<float> s_velocity;  // For click-free voice stealing

        Voice(float sample_rate) : envelope(sample_rate) {
            s_base_freq.reset(sample_rate, 0.005);  // Fast freq changes
            s_velocity.reset(sample_rate, 0.002);   // Very fast velocity ramps for stealing
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
            isActive = false;
            envelope.noteOff();
            // Don't immediately set velocity to 0 - let envelope handle the fade
        }

        void steal(uint8_t newNote, float newVel) {
            // For voice stealing: quickly fade out, change note, fade back in
            midiNote = newNote;
            velocity = newVel;

            // Quick fade to near-zero, then to new velocity
            s_velocity.setTargetValue(0.001f);  // Very quiet but not silent
            s_base_freq.setTargetValue(midi_note_to_freq(newNote));

            // The velocity will ramp back up after a few samples
            // We'll handle this in the process loop
        }

        bool shouldRampBackUp() const {
            // Return true when velocity has ramped down enough for clean transition
            return s_velocity.getCurrentValue() < 0.01f && !s_velocity.isSmoothing();
        }

        void completeSteal() {
            // Now ramp back up to full velocity and retrigger envelope
            s_velocity.setTargetValue(velocity);
            envelope.noteOn();  // Retrigger envelope
        }

        static float midi_note_to_freq(uint8_t note) {
            return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
        }
    };

public:
    DuophonicFMModule(float sample_rate) : sampleRate(sample_rate), voice1(sample_rate), voice2(sample_rate) {
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
        // --- 1. Handle incoming MIDI and Parameter changes ---
        update_control_signals();

        // --- 2. Generate audio from both voices ---
        auto numFrames = buffer.getNumFrames();
        auto numChannels = buffer.getNumChannels();

        // Clear buffer first
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            for (uint32_t f = 0; f < numFrames; ++f) {
                buffer.getSample(ch, f) = 0.0f;
            }
        }

        for (uint32_t f = 0; f < numFrames; ++f) {
            // Get global smoothed values per sample
            float current_mod_index = s_mod_index.getNextValue();
            float current_harmonicity = s_harmonicity.getNextValue();
            float current_master_vol = s_master_vol.getNextValue();

            float mixedSample = 0.0f;

            // Process voice 1
            if (voice1.envelope.isActive() || voice1.s_velocity.getCurrentValue() > 0.001f) {
                mixedSample += processVoice(voice1, current_mod_index, current_harmonicity, current_master_vol);
            }

            // Process voice 2
            if (voice2.envelope.isActive() || voice2.s_velocity.getCurrentValue() > 0.001f) {
                mixedSample += processVoice(voice2, current_mod_index, current_harmonicity, current_master_vol);
            }

            // Apply to all channels
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                buffer.getSample(ch, f) = mixedSample * 0.5f; // Mix level for 2 voices
            }
        }
    }

private:
    float processVoice(Voice& voice, float mod_index, float harmonicity, float master_vol) {
        // Handle voice stealing state machine
        if (voice.shouldRampBackUp() && voice.isActive) {
            voice.completeSteal();
        }

        // Get voice-specific smoothed values
        float current_base_freq = voice.s_base_freq.getNextValue();
        float current_velocity = voice.s_velocity.getNextValue();

        // Update modulator frequency
        voice.modulator.setFrequency(current_base_freq * harmonicity, sampleRate);

        // Generate modulator sample
        float mod_sample = voice.modulator.getSample();

        // Calculate carrier phase increment
        float carrier_phase_inc = two_pi * current_base_freq / sampleRate;

        // Generate FM synthesis output
        float out = sin(voice.carrier_phase + (mod_index * mod_sample));

        voice.carrier_phase += carrier_phase_inc;
        if (voice.carrier_phase >= two_pi) voice.carrier_phase -= two_pi;

        // Apply velocity and master volume
        float sample = out * current_velocity * master_vol;

        // Create a temporary buffer for this voice's envelope
        choc::buffer::InterleavedBuffer<float> voiceBuffer(1, 1);
        voiceBuffer.getSample(0, 0) = sample;
        auto voiceView = voiceBuffer.getView();

        // Apply envelope
        voice.envelope.process(voiceView);

        return voiceBuffer.getSample(0, 0);
    }

    void update_control_signals() {
        // Handle MIDI Note On/Off from the FIFO queue
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8)  & 0xFF;

            if (command == 0x90) { // NOTE_ON
                handleNoteOn(data1, data2 / 127.0f);
            } else if (command == 0x80) { // NOTE_OFF
                handleNoteOff(data1);
            }
        }

        // Update global parameter smoothers
        s_mod_index.setTargetValue(p_mod_index->getValue());
        s_harmonicity.setTargetValue(p_harmonicity->getValue());
        s_attack.setTargetValue(p_attack->getValue());
        s_decay.setTargetValue(p_decay->getValue());
        s_sustain.setTargetValue(p_sustain->getValue());
        s_release.setTargetValue(p_release->getValue());
        s_master_vol.setTargetValue(p_master_vol->getValue());

        // Apply envelope settings to both voices
        float attack = s_attack.getNextValue();
        float decay = s_decay.getNextValue();
        float sustain = s_sustain.getNextValue();
        float release = s_release.getNextValue();

        voice1.envelope.setAttackTime(attack);
        voice1.envelope.setDecayTime(decay);
        voice1.envelope.setSustainLevel(sustain);
        voice1.envelope.setReleaseTime(release);

        voice2.envelope.setAttackTime(attack);
        voice2.envelope.setDecayTime(decay);
        voice2.envelope.setSustainLevel(sustain);
        voice2.envelope.setReleaseTime(release);
    }

    void handleNoteOn(uint8_t note, float velocity) {
        // Voice allocation strategy:
        // 1. Use free voice if available
        // 2. Steal oldest voice if both are active

        if (!voice1.isActive) {
            voice1.noteOn(note, velocity);
            p_last_note_on_voice = &voice1; // Track that voice1 was the last one used
        } else if (!voice2.isActive) {
            voice2.noteOn(note, velocity);
            p_last_note_on_voice = &voice2; // Track that voice1 was the last one used
        } else {
            if (p_last_note_on_voice == &voice1) {
                // voice1 was played last, so voice2 is the older one. Steal it.
                voice2.steal(note, velocity);
                p_last_note_on_voice = &voice2; // Now voice2 is the newest
            } else {
                // voice2 was played last, so voice1 is the older one. Steal it.
                voice1.steal(note, velocity);
                p_last_note_on_voice = &voice1; // Now voice1 is the newest
            }
        }
    }

    void handleNoteOff(uint8_t note) {
        if (voice1.isActive && voice1.midiNote == note) {
            voice1.noteOff();
        }
        if (voice2.isActive && voice2.midiNote == note) {
            voice2.noteOff();
        }
    }

    // Voices
    Voice voice1, voice2;
    Voice* p_last_note_on_voice = &voice1; // <<< CHANGED: Pointer to the most recently used voice.

    float sampleRate;
    const float two_pi = 6.283185307179586f;

    // Pointers to the global parameters
    Parameter *p_mod_index, *p_harmonicity, *p_attack, *p_decay, *p_sustain, *p_release, *p_master_vol;

    // Global smoothers for shared parameters
    SmoothedValue<float> s_mod_index, s_harmonicity, s_attack, s_decay, s_sustain, s_release, s_master_vol;
};