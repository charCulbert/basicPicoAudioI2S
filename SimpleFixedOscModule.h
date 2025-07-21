/**
 * SimpleFixedOscModule.h - Complete Monophonic Synthesizer Voice
 * 
 * This module implements a complete synthesizer voice with:
 * - Fixed-point sine wave oscillator (lookup table based)
 * - ADSR envelope generator with smoothed parameter changes
 * - Master volume control
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

/**
 * Complete monophonic synthesizer voice module
 * 
 * Architecture:
 * - Oscillator -> Envelope -> Master Volume -> Output
 * - Dual-core design: control thread updates parameters, audio thread processes
 * - Thread-safe parameter smoothing prevents audio artifacts
 * - MIDI communication via multicore FIFO (note on/off)
 * 
 * Parameter Flow:
 * 1. HTML/MIDI -> Parameter Store (control thread)
 * 2. Audio thread reads parameters and updates smoothers
 * 3. Smoothers provide artifact-free parameter changes
 * 4. Audio processing uses only smoothed fix15 values
 */
class SimpleFixedOscModule : public AudioModule {
private:
    // === Audio Processing Components ===
    fixOscs::oscillator::Sine oscillator;    // High-quality sine wave generator with lookup table
    float sampleRate;                        // Sample rate (stored for frequency calculations)
    Fix15VCAEnvelopeModule envelope;         // ADSR envelope generator (VCA = Voltage Controlled Amplifier)
    
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
    
    // === Debug/Development Support ===
    float last_master_vol = 0.0f;         // Used for tracking parameter changes during development

public:
    SimpleFixedOscModule(float sample_rate)
    : sampleRate(sample_rate), envelope(sample_rate) {
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

        // Initialize envelope with parameter values from store
        if (p_attack) envelope.setAttackTime(p_attack->getValue());
        if (p_decay) envelope.setDecayTime(p_decay->getValue());
        if (p_sustain) envelope.setSustainLevel(p_sustain->getValue());
        if (p_release) envelope.setReleaseTime(p_release->getValue());
        
        // Set up the oscillator with initial frequency
        setFrequency(440.0f);
        
    }

    void setFrequency(float freq_hz) {
        oscillator.setFrequency(freq_hz, sampleRate);
    }

    void process(choc::buffer::InterleavedView<fix15>& buffer) override {
        auto numFrames = buffer.getNumFrames();

        // Update parameters once per buffer (more efficient)
        updateControlSignals();
        
        for (uint32_t f = 0; f < numFrames; ++f) {
            
            // Get smoothed values (pure fix15 - no floats in audio thread!)
            fix15 current_master_vol = s_master_vol.getNextValue();
            
            // Get envelope value
            fix15 env_level = envelope.getNextValue();
            
            // Get the next sample from the oscillator
            fix15 sine_sample = oscillator.getSample();
            
            // Apply envelope to sample
            fix15 enveloped_sample = multfix15(sine_sample, env_level);
            
            // Apply master volume (divided by 8)
            fix15 finalSample = multfix15(enveloped_sample, (current_master_vol >> 3));

            // Output to all channels
            for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
                buffer.getSample(ch, f) = finalSample;
            }
        }
    }

private:
    // Called from audio thread - updates smoothers with new targets from control thread
    void updateControlSignals() {
        // Update parameters from parameter store
        if (p_master_vol) s_master_vol.setTargetValue(p_master_vol->getValue());
        
        // Update envelope parameters (prevent crackling by only updating A/D/R when idle)
        if (p_attack && p_decay && p_sustain && p_release) {
            float sustainValue = p_sustain->getValue();
            
            // DEBUG: Print sustain parameter changes
            static float lastSustain = -1.0f;
            if (fabsf(sustainValue - lastSustain) > 0.01f) {
                // printf("PARAM_SUSTAIN: %.4f\n", sustainValue);
                lastSustain = sustainValue;
            }
            
            // Always update sustain (smoothed)
            envelope.setSustainLevel(sustainValue);
            
            // Only update A/D/R when envelope is not active (prevents crackling mid-note)
            if (!envelope.isActive()) {
                envelope.setAttackTime(p_attack->getValue());
                envelope.setDecayTime(p_decay->getValue());
                envelope.setReleaseTime(p_release->getValue());
            }
        }
        
        // Handle MIDI frequency changes from multicore FIFO
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            
            if (command == 0x90) { // Note on
                float freq = midiNoteToFreq(data1);
                oscillator.setFrequency(freq, sampleRate);
                envelope.noteOn();
            } else if (command == 0x80) { // Note off
                envelope.noteOff();
            }
        }
    }
    
    // Helper function to convert MIDI note to frequency
    static float midiNoteToFreq(uint8_t note) {
        return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
    }
};