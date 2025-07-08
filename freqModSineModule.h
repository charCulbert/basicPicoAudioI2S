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
            envelope.noteOn(); // This is key: always retrigger the envelope
        }

        void noteOff() {
            // A note off for a voice that is not active can happen
            // if it was stolen. In that case, we just ignore it.
            if (!isActive) return;

            isActive = false;
            envelope.noteOff();
            // Don't set velocity to 0. The envelope release stage handles the fade-out.
            // s_velocity smoother will just hold the last value, which is fine.
        }

        // <<< CHANGED: The complex steal(), shouldRampBackUp(), and completeSteal() methods have been removed.
        // The noteOn() method is now used for both new notes and stolen notes.

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

        p_last_note_on_voice = &voice2; // Initialize to avoid stealing voice1 first
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        update_control_signals();

        auto numFrames = buffer.getNumFrames();

        for (uint32_t f = 0; f < numFrames; ++f) {
            // Get global smoothed values per sample
            float current_mod_index = s_mod_index.getNextValue();
            float current_harmonicity = s_harmonicity.getNextValue();
            float current_master_vol = s_master_vol.getNextValue();

            float mixedSample = 0.0f;

            // A voice is audible if its envelope is still active.
            if (voice1.envelope.isActive()) {
                mixedSample += processVoice(voice1, current_mod_index, current_harmonicity);
            }

            if (voice2.envelope.isActive()) {
                mixedSample += processVoice(voice2, current_mod_index, current_harmonicity);
            }

            // Apply master volume and mix level to all channels
            float finalSample = mixedSample * 0.5f * current_master_vol;
            for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, f) = finalSample;
            }
        }
    }

private:
    float processVoice(Voice& voice, float mod_index, float harmonicity) {
        // <<< CHANGED: Removed the voice stealing state machine check. It's no longer needed.

        // Get voice-specific smoothed values
        float current_base_freq = voice.s_base_freq.getNextValue();
        float current_velocity = voice.s_velocity.getNextValue();

        // Update modulator frequency
        voice.modulator.setFrequency(current_base_freq * harmonicity, sampleRate);
        float mod_sample = voice.modulator.getSample();

        // Calculate carrier phase increment
        float carrier_phase_inc = two_pi * current_base_freq / sampleRate;
        float out = sin(voice.carrier_phase + (mod_index * mod_sample));
        voice.carrier_phase += carrier_phase_inc;
        if (voice.carrier_phase >= two_pi) voice.carrier_phase -= two_pi;

        // Apply velocity (which scales the envelope's peak)
        float sample = out * current_velocity;

        // Create a temporary buffer for this voice's envelope processing
        choc::buffer::InterleavedBuffer<float> voiceBuffer(1, 1);
        voiceBuffer.getSample(0, 0) = sample;
        auto voiceView = voiceBuffer.getView();

        // The envelope acts as the final VCA
        voice.envelope.process(voiceView);

        return voiceBuffer.getSample(0, 0);
    }

    void update_control_signals() {
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8)  & 0xFF;

            if (command == 0x90 && data2 > 0) { // Note On
                handleNoteOn(data1, data2 / 127.0f);
            } else if (command == 0x80 || (command == 0x90 && data2 == 0)) { // Note Off
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
        // Voice allocation: Find a free voice, or steal the oldest one.
        // A voice is "free" if its envelope has finished its release phase.
        if (!voice1.envelope.isActive()) {
            voice1.noteOn(note, velocity);
            p_last_note_on_voice = &voice1;
        } else if (!voice2.envelope.isActive()) {
            voice2.noteOn(note, velocity);
            p_last_note_on_voice = &voice2;
        } else {
            // Both voices are active, so steal the one that wasn't played last.
            if (p_last_note_on_voice == &voice1) {
                // voice1 was played last, so steal voice2.
                voice2.noteOn(note, velocity); // <<< CHANGED: Just call noteOn()
                p_last_note_on_voice = &voice2;
            } else {
                // voice2 was played last, so steal voice1.
                voice1.noteOn(note, velocity); // <<< CHANGED: Just call noteOn()
                p_last_note_on_voice = &voice1;
            }
        }
    }

    void handleNoteOff(uint8_t note) {
        // The noteOff message should apply to the voice currently playing that note.
        if (voice1.isActive && voice1.midiNote == note) {
            voice1.noteOff();
        }
        if (voice2.isActive && voice2.midiNote == note) {
            voice2.noteOff();
        }
    }

    // Voices
    Voice voice1, voice2;
    Voice* p_last_note_on_voice;

    float sampleRate;
    const float two_pi = 6.283185307179586f;

    // Pointers to the global parameters
    Parameter *p_mod_index, *p_harmonicity, *p_attack, *p_decay, *p_sustain, *p_release, *p_master_vol;

    // Global smoothers for shared parameters
    SmoothedValue<float> s_mod_index, s_harmonicity, s_attack, s_decay, s_sustain, s_release, s_master_vol;
};