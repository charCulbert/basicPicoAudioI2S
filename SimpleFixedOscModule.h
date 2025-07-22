// Polyphonic synthesizer module with oscillators, envelopes, and voice allocation

#pragma once

#include "AudioModule.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "pico/multicore.h"
#include "Fix15Oscillators.h"
#include "Fix15VCAEnvelopeModule.h"
#include <vector>

class SimpleFixedOscModule : public AudioModule {
private:
    // Number of polyphonic voices
    static constexpr int NUM_VOICES = 8;
    
    struct Voice {
        // DSP objects per voice
        fixOscs::oscillator::Saw oscillator;
        Fix15VCAEnvelopeModule envelope;
        
        // Voice state
        uint8_t midiNote = 0;
        bool isActive = false;
        fix15 velocity = 0;
        
        // Per-voice smoothers
        Fix15SmoothedValue s_velocity;

        Voice(float sample_rate) : envelope(sample_rate) {
            s_velocity.reset(sample_rate, 0.005);  // Fast velocity changes (5ms)
            s_velocity.setValue(0);
        }
        
        void noteOn(uint8_t note, fix15 vel, float sample_rate) {
            midiNote = note;
            isActive = true;
            velocity = vel;
            
            // OPTIMIZED: Set frequency immediately - envelope StealFade handles smooth stealing
            float freq = midiNoteToFreq(note);
            oscillator.setFrequency(freq, sample_rate);
            s_velocity.setTargetValue(vel);
            envelope.noteOn(); // This handles StealFade for smooth voice stealing
        }
        
        void noteOff() {
            if (!isActive) return;
            isActive = false;
            envelope.noteOff();
        }
        
        static float midiNoteToFreq(uint8_t note) {
            // OPTIMIZED: Use lookup table instead of expensive powf() calculation
            static const float midi_freq_table[128] = {
                8.176f, 8.662f, 9.177f, 9.723f, 10.301f, 10.913f, 11.562f, 12.250f, 12.978f, 13.750f, 14.568f, 15.434f, // C-1 to B-1
                16.352f, 17.324f, 18.354f, 19.445f, 20.602f, 21.827f, 23.125f, 24.500f, 25.957f, 27.500f, 29.135f, 30.868f, // C0 to B0
                32.703f, 34.648f, 36.708f, 38.891f, 41.203f, 43.654f, 46.249f, 48.999f, 51.913f, 55.000f, 58.270f, 61.735f, // C1 to B1
                65.406f, 69.296f, 73.416f, 77.782f, 82.407f, 87.307f, 92.499f, 97.999f, 103.826f, 110.000f, 116.541f, 123.471f, // C2 to B2
                130.813f, 138.591f, 146.832f, 155.563f, 164.814f, 174.614f, 184.997f, 195.998f, 207.652f, 220.000f, 233.082f, 246.942f, // C3 to B3
                261.626f, 277.183f, 293.665f, 311.127f, 329.628f, 349.228f, 369.994f, 391.995f, 415.305f, 440.000f, 466.164f, 493.883f, // C4 to B4
                523.251f, 554.365f, 587.330f, 622.254f, 659.255f, 698.456f, 739.989f, 783.991f, 830.609f, 880.000f, 932.328f, 987.767f, // C5 to B5
                1046.502f, 1108.731f, 1174.659f, 1244.508f, 1318.510f, 1396.913f, 1479.978f, 1567.982f, 1661.219f, 1760.000f, 1864.655f, 1975.533f, // C6 to B6
                2093.005f, 2217.461f, 2349.318f, 2489.016f, 2637.020f, 2793.826f, 2959.955f, 3135.963f, 3322.438f, 3520.000f, 3729.310f, 3951.066f, // C7 to B7
                4186.009f, 4434.922f, 4698.636f, 4978.032f, 5274.041f, 5587.652f, 5919.911f, 6271.927f, 6644.875f, 7040.000f, 7458.620f, 7902.133f, // C8 to B8
                8372.018f, 8869.844f, 9397.273f, 9956.063f, 10548.082f, 11175.303f, 11839.822f, 12543.854f // C9 to G9
            };
            
            if (note >= 128) note = 127; // Clamp to valid range
            return midi_freq_table[note];
        }
    };
    
    // Voice management
    std::vector<Voice> voices;
    size_t next_voice_to_steal = 0;
    
    float sampleRate;                        // Sample rate (stored for frequency calculations)
    
    // === Parameter System ===
    // Pointers to global parameters (shared between control and audio threads)
    // These are read-only from the audio thread perspective
    Parameter* p_attack = nullptr;
    Parameter* p_decay = nullptr;
    Parameter* p_sustain = nullptr;
    Parameter* p_release = nullptr;
    
    
    // === Audio Thread Smoothers ===
    // These convert parameter changes into smooth audio-rate transitions
    // Prevents clicks/pops when parameters change during audio processing
    // CRITICAL: Audio thread uses ONLY these smoothed values, never raw parameters
    Fix15SmoothedValue s_attack;           // Note: A/D/R smoothers exist but envelope uses direct updates
    Fix15SmoothedValue s_decay;            // They're kept for potential future use
    Fix15SmoothedValue s_sustain;
    Fix15SmoothedValue s_release;


public:
    SimpleFixedOscModule(float sample_rate)
    : sampleRate(sample_rate) {
        // Initialize voices
        voices.reserve(NUM_VOICES);
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices.emplace_back(sample_rate);
        }
        
        // Find our parameters by their string ID from the global store
        for (auto* p : g_synth_parameters) {
            if (p->getID() == "attack") p_attack = p;
            if (p->getID() == "decay") p_decay = p;
            if (p->getID() == "sustain") p_sustain = p;
            if (p->getID() == "release") p_release = p;
        }
        
        // Set ramp times for smoothers
        s_attack.reset(sample_rate, 0.01);
        s_decay.reset(sample_rate, 0.01);
        s_sustain.reset(sample_rate, 0.01);
        s_release.reset(sample_rate, 0.01);
        
        // Initialize smoothers with current values

        // Initialize all voice envelopes with parameter values from store
        for (auto& voice : voices) {
            if (p_attack) voice.envelope.setAttackTime(p_attack->getValue());
            if (p_decay) voice.envelope.setDecayTime(p_decay->getValue());
            if (p_sustain) voice.envelope.setSustainLevel(p_sustain->getValue());
            if (p_release) voice.envelope.setReleaseTime(p_release->getValue());
        }
    }


    void process(choc::buffer::InterleavedView<fix15>& buffer) override {
        auto numFrames = buffer.getNumFrames();

        // Update parameters once per buffer (more efficient)
        updateControlSignals();
        
        for (uint32_t f = 0; f < numFrames; ++f) {
            
            
            // Use 32-bit accumulator to prevent overflow
            int32_t mixedSample32 = 0;
            
            // Process all active voices and mix their output
            for (auto& voice : voices) {
                if (voice.envelope.isActive()) {
                    mixedSample32 += processVoice(voice); // Accumulate in 32-bit to prevent overflow
                }
            }
            
            // Scale down by voice count - use bit shift for performance
            fix15 finalSample = (fix15)(mixedSample32 >> 3); // Divide by 8 using bit shift

        // Output to all channels
            for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, f) = finalSample;
            }
        }
    }

private:
    fix15 processVoice(Voice& voice) {
        // OPTIMIZED: Removed per-sample frequency smoothing - frequency is set once per note
        fix15 current_velocity = voice.s_velocity.getNextValue();
        
        // Get envelope value (this handles StealFade for smooth voice stealing)
        fix15 env_level = voice.envelope.getNextValue();
        
        // Get the next sample from the oscillator
        fix15 sine_sample = voice.oscillator.getSample();
        
        // Apply envelope to sample
        fix15 enveloped_sample = multfix15(sine_sample, env_level);
        
        // Apply velocity
        return multfix15(enveloped_sample, current_velocity);
    }
    
    // Called from audio thread - updates smoothers with new targets from control thread
    void updateControlSignals() {
        // Handle MIDI messages from multicore FIFO
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8) & 0xFF;
            
            if (command == 0x90 && data2 > 0) { // Note on
                fix15 velocity = (data2 << 8); // Convert MIDI velocity (0-127) to fix15
                handleNoteOn(data1, velocity);
            } else if (command == 0x80 || (command == 0x90 && data2 == 0)) { // Note off
                handleNoteOff(data1);
            } else if (command == 0xB0 && data1 == 123) { // All Notes Off (CC 123)
                handleAllNotesOff();
            }
        }
        
        // Update parameters from parameter store
        
        // Update envelope parameters for all voices
        if (p_attack && p_decay && p_sustain && p_release) {
            float attackValue = p_attack->getValue();
            float decayValue = p_decay->getValue();
            float sustainValue = p_sustain->getValue();
            float releaseValue = p_release->getValue();
            
            // Update all voice envelopes - classic analog synth behavior with smoothing
            for (auto& voice : voices) {
                voice.envelope.setAttackTime(attackValue);
                voice.envelope.setDecayTime(decayValue);
                voice.envelope.setSustainLevel(sustainValue);
                voice.envelope.setReleaseTime(releaseValue);
            }
        }
    }
    
    void handleNoteOn(uint8_t note, fix15 velocity) {
        // 1. Find the first completely idle voice
        for (auto& voice : voices) {
            if (!voice.envelope.isActive()) {
                voice.noteOn(note, velocity, sampleRate);
                return; // Found a free voice, we're done
            }
        }
        
        // 2. If no idle voices, look for voices in release phase (prefer stealing these)
        for (auto& voice : voices) {
            if (voice.envelope.getState() == Fix15VCAEnvelopeModule::State::Release) {
                voice.noteOn(note, velocity, sampleRate); // This will do a clean restart, not StealFade
                return; // Found a releasing voice to reuse
            }
        }
        
        // 3. If all voices are actively playing (Attack/Decay/Sustain), steal one using round-robin
        voices[next_voice_to_steal].noteOn(note, velocity, sampleRate);
        next_voice_to_steal = (next_voice_to_steal + 1) % NUM_VOICES;
    }
    
    void handleNoteOff(uint8_t note) {
        // Find the voice playing this note and turn it off
        for (auto& voice : voices) {
            if (voice.isActive && voice.midiNote == note) {
                voice.noteOff();
                return; // A note should only be active on one voice at a time
            }
        }
    }
    
    void handleAllNotesOff() {
        // Turn off all active voices immediately
        for (auto& voice : voices) {
            if (voice.isActive) {
                voice.noteOff();
            }
        }
    }
};