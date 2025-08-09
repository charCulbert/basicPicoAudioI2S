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

// Simple single-voice Moog ladder filter for per-voice filtering
class VoiceFilter {
public:
    VoiceFilter() {
        stage1 = stage2 = stage3 = stage4 = 0;
    }
    
    fix15 process(fix15 input, fix15 cutoff, fix15 resonance) {
        // Map cutoff: 0-1 -> 0.001-0.85 
        fix15 g = multfix15(cutoff, 27787) + 33;  // 0.849 * 32768, 0.001 * 32768
        // Map resonance: 0-1 -> 0-4.5
        fix15 res = multfix15(resonance, 147456);  // 4.5 * 32768
        
        // Moog ladder with resonance feedback
        fix15 fb_input = input - multfix15(res, stage4);
        
        // Clamp feedback input to prevent instability
        if (fb_input > FIX15_ONE) fb_input = FIX15_ONE;
        else if (fb_input < -FIX15_ONE) fb_input = -FIX15_ONE;
        
        // 4-stage ladder filter
        stage1 = stage1 + multfix15(g, fb_input - stage1);
        stage2 = stage2 + multfix15(g, stage1 - stage2);
        stage3 = stage3 + multfix15(g, stage2 - stage3);
        stage4 = stage4 + multfix15(g, stage3 - stage4);
        
        // Makeup gain (2.5x) to compensate for filter loss
        fix15 output = stage4 + (stage4 >> 1);  // * 1.5, then we'll add more
        output = output + (output >> 2);        // * 1.25 more = 1.5 * 1.25 = 1.875
        output = output + (output >> 3);        // * 1.125 more = 1.875 * 1.125 ≈ 2.1
        
        return output;
    }
    
private:
    fix15 stage1, stage2, stage3, stage4;
};

class SimpleFixedOscModule : public AudioModule {
private:
    // Number of polyphonic voices
    static constexpr int NUM_VOICES = 4;
    
    struct Voice {
        // DSP objects per voice
        fixOscs::oscillator::Saw sawOsc;
        fixOscs::oscillator::Pulse pulseOsc;
        fixOscs::oscillator::Square subOsc;  // Sub oscillator (1 octave down square wave)
        fixOscs::oscillator::Noise noiseOsc;
        Fix15VCAEnvelopeModule envelope;
        VoiceFilter filter;  // Per-voice filter for envelope modulation
        
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
            
            // Reset phases for consistent oscillator synchronization
            sawOsc.resetPhase();
            pulseOsc.resetPhase();
            subOsc.resetPhase();
            noiseOsc.resetPhase();
            
            // Set frequencies after phase reset
            sawOsc.setFrequency(freq, sample_rate);
            pulseOsc.setFrequency(freq, sample_rate);
            subOsc.setFrequency(freq, sample_rate, 0.5f);  // Sub is exactly half frequency (octave down)
            // Noise doesn't need frequency setting
            s_velocity.setTargetValue(vel);
            envelope.noteOn(); // This handles StealFade for smooth voice stealing
        }
        
        void noteOff() {
            if (!isActive) return;
            isActive = false;  // Key is no longer pressed
            envelope.noteOff(); // Start release phase
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
    Parameter* p_sawLevel = nullptr;
    Parameter* p_pulseLevel = nullptr;
    Parameter* p_subLevel = nullptr;
    Parameter* p_noiseLevel = nullptr;
    Parameter* p_pulseWidth = nullptr;
    Parameter* p_filterCutoff = nullptr;
    Parameter* p_filterResonance = nullptr;
    Parameter* p_filterEnvAmount = nullptr;
    Parameter* p_filterKeyboardTracking = nullptr;
    
    
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
            if (p->getID() == "sawLevel") p_sawLevel = p;
            if (p->getID() == "pulseLevel") p_pulseLevel = p;
            if (p->getID() == "subLevel") p_subLevel = p;
            if (p->getID() == "noiseLevel") p_noiseLevel = p;
            if (p->getID() == "pulseWidth") p_pulseWidth = p;
            if (p->getID() == "filterCutoff") p_filterCutoff = p;
            if (p->getID() == "filterResonance") p_filterResonance = p;
            if (p->getID() == "filterEnvAmount") p_filterEnvAmount = p;
            if (p->getID() == "filterKeyboardTracking") p_filterKeyboardTracking = p;
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
            
            // Scale down by voice count for suitable codec/headphone level - use bit shift for performance
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
        
        // Update pulse width for this voice
        if (p_pulseWidth) {
            fix15 pulseWidth = float2fix15(p_pulseWidth->getValue());
            voice.pulseOsc.setPulseWidth(pulseWidth);
        }
        
        // Get oscillator samples
        fix15 saw_sample = voice.sawOsc.getSample();
        fix15 pulse_sample = voice.pulseOsc.getSample();
        fix15 sub_sample = voice.subOsc.getSample();  // Back to separate sub oscillator
        fix15 noise_sample = voice.noiseOsc.getSample();
        
        // Get mix levels (SH-101 style - each oscillator has independent level)
        fix15 sawLevel = p_sawLevel ? float2fix15(p_sawLevel->getValue()) : FIX15_ONE;
        fix15 pulseLevel = p_pulseLevel ? float2fix15(p_pulseLevel->getValue()) : FIX15_ZERO;
        fix15 subLevel = p_subLevel ? float2fix15(p_subLevel->getValue()) : FIX15_ZERO;
        fix15 noiseLevel = p_noiseLevel ? float2fix15(p_noiseLevel->getValue()) : FIX15_ZERO;
        
        // Mix oscillators additively (like SH-101)
        int32_t mixed_sample32 = multfix15(saw_sample, sawLevel) + 
                                multfix15(pulse_sample, pulseLevel) + 
                                multfix15(sub_sample, subLevel) +
                                multfix15(noise_sample, noiseLevel);
        
        // Pure additive oscillator mixing with safe casting
        // Scale down just enough to prevent int16_t overflow, but preserve mixing behavior
        mixed_sample32 = mixed_sample32 >> 2;  // Divide by 4 to allow up to 4 oscillators at full level
        fix15 mixed_sample = (fix15)mixed_sample32;
        
        // Apply per-voice filter with envelope and keyboard tracking modulation
        fix15 base_cutoff = p_filterCutoff ? float2fix15(p_filterCutoff->getValue()) : float2fix15(0.5f);
        fix15 env_amount = p_filterEnvAmount ? float2fix15(p_filterEnvAmount->getValue()) : FIX15_ZERO;
        fix15 kbd_amount = p_filterKeyboardTracking ? float2fix15(p_filterKeyboardTracking->getValue()) : FIX15_ZERO;
        fix15 resonance = p_filterResonance ? float2fix15(p_filterResonance->getValue()) : float2fix15(0.2f);
        
        // Calculate keyboard tracking offset (relative to C4 = MIDI note 60)
        // Each octave up/down adds/subtracts a smaller amount for musical tracking
        int note_offset = (int)voice.midiNote - 60;  // Distance from C4
        fix15 kbd_offset = multfix15(float2fix15((note_offset / 12.0f) * 0.3f), kbd_amount);  // Convert to octaves, scale down by 30%
        
        // Modulate filter cutoff: base + envelope + keyboard tracking
        fix15 modulated_cutoff = base_cutoff + multfix15(env_level, env_amount) + kbd_offset;
        
        // Clamp cutoff to valid range (0-1)
        if (modulated_cutoff > FIX15_ONE) modulated_cutoff = FIX15_ONE;
        else if (modulated_cutoff < FIX15_ZERO) modulated_cutoff = FIX15_ZERO;
        
        // Apply per-voice filter
        fix15 filtered_sample = voice.filter.process(mixed_sample, modulated_cutoff, resonance);
        
        // Apply envelope to filtered sample
        fix15 enveloped_sample = multfix15(filtered_sample, env_level);
        
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
        // 1. FIRST: Check if this note is already playing OR still sounding - steal it immediately
        for (auto& voice : voices) {
            if (voice.midiNote == note && (voice.isActive || voice.envelope.isActive())) {
                voice.noteOn(note, velocity, sampleRate); // Retrigger the same note
                return; // Don't allow duplicate notes, even if in release phase
            }
        }
        
        // 2. Find the first completely idle voice
        for (auto& voice : voices) {
            if (!voice.envelope.isActive()) {
                voice.noteOn(note, velocity, sampleRate);
                return; // Found a free voice, we're done
            }
        }
        
        // 3. If no idle voices, look for voices in release phase (prefer stealing these)
        for (auto& voice : voices) {
            if (voice.envelope.getState() == Fix15VCAEnvelopeModule::State::Release) {
                voice.noteOn(note, velocity, sampleRate);
                return; // Found a releasing voice to reuse
            }
        }
        
        // 4. ONLY IF MORE THAN 4 NOTES: steal the oldest voice (FIFO)
        // This should rarely happen with 4 voices - only when playing more than 4 simultaneous notes
        size_t oldest_voice = 0;
        for (size_t i = 1; i < voices.size(); ++i) {
            // Find voice that's been playing longest (simple heuristic: lowest envelope level in sustain)
            if (voices[i].envelope.getState() == Fix15VCAEnvelopeModule::State::Sustain &&
                voices[oldest_voice].envelope.getState() == Fix15VCAEnvelopeModule::State::Sustain) {
                // Both in sustain, steal the one with lower envelope level (been playing longer)
                if (voices[i].envelope.getNextValue() < voices[oldest_voice].envelope.getNextValue()) {
                    oldest_voice = i;
                }
            }
        }
        voices[oldest_voice].noteOn(note, velocity, sampleRate);
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