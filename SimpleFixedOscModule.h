/**
 * SimpleFixedOscModule.h - Complete Polyphonic Synthesizer Voice
 * 
 * This module implements a complete polyphonic synthesizer with:
 * - Fixed-point sine wave oscillators (lookup table based) per voice
 * - ADSR envelope generators with smoothed parameter changes per voice
 * - Master volume control
 * - Voice allocation and stealing logic
 * - MIDI note on/off handling via dual-core FIFO communication
 * - Thread-safe parameter updates from control thread
 * 
 * All audio processing uses 16.15 fixed-point arithmetic for optimal
 * performance on the RP2040 microcontroller.
 */

#pragma once

#include "AudioModule.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "pico/multicore.h"
#include "Fix15Oscillators.h"
#include "Fix15VCAEnvelopeModule.h"
#include <vector>

/**
 * Complete polyphonic synthesizer voice module
 * 
 * Architecture:
 * - Multiple Voices: Oscillator -> Envelope -> Master Volume -> Mixed Output
 * - Dual-core design: control thread updates parameters, audio thread processes
 * - Thread-safe parameter smoothing prevents audio artifacts
 * - MIDI communication via multicore FIFO (note on/off)
 * - Voice allocation with round-robin stealing when all voices busy
 * 
 * Parameter Flow:
 * 1. HTML/MIDI -> Parameter Store (control thread)
 * 2. Audio thread reads parameters and updates smoothers
 * 3. Smoothers provide artifact-free parameter changes
 * 4. Audio processing uses only smoothed fix15 values
 */
class SimpleFixedOscModule : public AudioModule {
private:
    // Number of polyphonic voices
    static constexpr int NUM_VOICES = 8;
    
    struct Voice {
        // DSP objects per voice
        fixOscs::oscillator::Sine oscillator;    // High-quality sine wave generator with lookup table
        Fix15VCAEnvelopeModule envelope;         // ADSR envelope generator (VCA = Voltage Controlled Amplifier)
        
        // Voice state
        uint8_t midiNote = 0;
        bool isActive = false;
        fix15 velocity = 0;
        
        // Per-voice smoothers
        Fix15SmoothedValue s_velocity;
        
        Voice(float sample_rate) : envelope(sample_rate) {
            s_velocity.reset(sample_rate, 0.005);  // Fast velocity changes
            s_velocity.setValue(0);
        }
        
        void noteOn(uint8_t note, fix15 vel, float sample_rate) {
            midiNote = note;
            isActive = true;
            velocity = vel;
            
            float freq = midiNoteToFreq(note);
            oscillator.setFrequency(freq, sample_rate);
            s_velocity.setTargetValue(vel);
            envelope.noteOn();
        }
        
        void noteOff() {
            if (!isActive) return;
            isActive = false;
            envelope.noteOff();
        }
        
        static float midiNoteToFreq(uint8_t note) {
            return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
        }
    };
    
    // Voice management
    std::vector<Voice> voices;
    size_t next_voice_to_steal = 0;
    
    float sampleRate;                        // Sample rate (stored for frequency calculations)
    
    // === Parameter System ===
    // Pointers to global parameters (shared between control and audio threads)
    // These are read-only from the audio thread perspective
    Parameter* p_master_vol = nullptr;
    Parameter* p_attack = nullptr;
    Parameter* p_decay = nullptr;
    Parameter* p_sustain = nullptr;
    Parameter* p_release = nullptr;
    
    
    // === Audio Thread Smoothers ===
    // These convert parameter changes into smooth audio-rate transitions
    // Prevents clicks/pops when parameters change during audio processing
    // CRITICAL: Audio thread uses ONLY these smoothed values, never raw parameters
    Fix15SmoothedValue s_master_vol;
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
            if (p->getID() == "masterVol") p_master_vol = p;
            if (p->getID() == "attack") p_attack = p;
            if (p->getID() == "decay") p_decay = p;
            if (p->getID() == "sustain") p_sustain = p;
            if (p->getID() == "release") p_release = p;
        }
        
        // Set ramp times for smoothers
        s_master_vol.reset(sample_rate, 0.05);
        s_attack.reset(sample_rate, 0.01);
        s_decay.reset(sample_rate, 0.01);
        s_sustain.reset(sample_rate, 0.01);
        s_release.reset(sample_rate, 0.01);
        
        // Initialize smoothers with current values
        if (p_master_vol) s_master_vol.setValue(p_master_vol->getValue());

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
            
            // Get smoothed master volume (pure fix15 - no floats in audio thread!)
            fix15 current_master_vol = s_master_vol.getNextValue();
            
            fix15 mixedSample = 0;
            
            // Process all active voices and mix their output
            for (auto& voice : voices) {
                if (voice.envelope.isActive()) {
                    mixedSample += processVoice(voice);
                }
            }
            
            // Apply master volume with voice mixing normalization (divided by NUM_VOICES and then by 8)
            fix15 finalSample = multfix15(mixedSample, (current_master_vol >> (3 + 2))); // /8 for volume, /4 for voice mixing

            // Output to all channels
            for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, f) = finalSample;
            }
        }
    }

private:
    fix15 processVoice(Voice& voice) {
        // Get smoothed velocity value
        fix15 current_velocity = voice.s_velocity.getNextValue();
        
        // Get envelope value
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
            }
        }
        
        // Update parameters from parameter store
        if (p_master_vol) s_master_vol.setTargetValue(p_master_vol->getValue());
        
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
        // 1. Find the first available (inactive) voice
        for (auto& voice : voices) {
            if (!voice.envelope.isActive()) {
                voice.noteOn(note, velocity, sampleRate);
                return; // Found a free voice, we're done
            }
        }
        
        // 2. If all voices are busy, steal one using round-robin scheme
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
};