/**
 * Fix15VCAEnvelopeModule.h - Fixed-Point ADSR Envelope Generator
 * 
 * Implements a classic ADSR (Attack-Decay-Sustain-Release) envelope generator
 * using pure 16.15 fixed-point arithmetic for optimal RP2040 performance.
 * 
 * Key Features:
 * - Pure fix15 arithmetic in audio thread (no floating-point operations)
 * - Smoothed sustain level changes prevent parameter zipper noise
 * - Counter-based timing for predictable, sample-accurate envelopes
 * - Sub-audio thresholding ensures true silence when sustain = 0
 * - Can function as both envelope generator and VCA (Voltage Controlled Amplifier)
 * 
 * Envelope Phases:
 * - Attack: Rise from 0 to 1 over specified time
 * - Decay: Fall from 1 to sustain level over specified time  
 * - Sustain: Hold at sustain level until note off
 * - Release: Fall from current level to 0 over specified time
 * - Idle: Silent state (level = 0)
 */

#pragma once

#include "AudioModule.h"
#include "SmoothedValue.h"
#include "Fix15.h"
#include "choc/audio/choc_SampleBuffers.h"
#include <cmath>
#include <algorithm>

/**
 * ADSR envelope generator with VCA functionality
 * 
 * Uses hybrid approach for timing:
 * - Audio signals: fix15 for performance
 * - Envelope timing: 32-bit sample counting with floating-point increments
 * - Avoids fix15 overflow issues while maintaining audio thread performance
 * 
 * This module can operate in two modes:
 * 1. Pure envelope generator: Call getNextValue() to get envelope level
 * 2. VCA mode: Call process() to apply envelope to incoming audio buffer
 * 
 * Threading Model:
 * - Parameter updates (setAttackTime, etc.) called from control thread
 * - Audio processing (getNextValue, process) called from audio thread
 * - Sustain level changes are smoothed to prevent zipper noise
 */
class Fix15VCAEnvelopeModule : public AudioModule {
public:
    // Envelope state machine phases
    enum class State { Idle, Attack, Decay, Sustain, Release, StealFade };

    Fix15VCAEnvelopeModule(float sampleRate) : sampleRate(sampleRate) {
        s_sustainLevel.reset(sampleRate, 0.01); // 10ms smoothing for sustain changes
        s_sustainLevel.setValue(sustainLevel);
        
        // Initialize smoothed sample counts (50ms smoothing to prevent clicks)
        s_attackSamples.reset(sampleRate, 0.05);
        s_decaySamples.reset(sampleRate, 0.05);
        s_releaseSamples.reset(sampleRate, 0.05);
        
        // Initialize with default sample counts
        s_attackSamples.setValue(attackSamples);
        s_decaySamples.setValue(decaySamples);
        s_releaseSamples.setValue(releaseSamples);
        
        // StealFade timing: very short (5ms) to minimize delay but prevent clicks
        stealFadeTimeSeconds = 0.005f;
        stealFadeSamples = (uint32_t)(stealFadeTimeSeconds * sampleRate);
        
        updateSampleCounts();
    }

    void noteOn() {
        if (currentLevel > FIX15_ZERO) {
            // FIXED: Always use StealFade when current level > 0 to prevent clicks
            // This includes voices in Release state - they need to fade to zero first
            state = State::StealFade;
            stealFadeStartLevel = currentLevel;
            sampleCounter = 0;
        } else {
            // Voice is free (Idle with level = 0) - start attack immediately
            currentLevel = FIX15_ZERO;
            state = State::Attack;
            sampleCounter = 0;
        }
    }

    void noteOff() {
        if (state != State::Idle) {
            releaseStartLevel = currentLevel; // Remember level when release started
            state = State::Release;
            sampleCounter = 0; // Reset counter for release phase
        }
    }

    bool isActive() const { return state != State::Idle; }
    State getState() const { return state; }

    void setAttackTime(float seconds) {
        attackTimeSeconds = std::max(0.001f, seconds);
        attackSamples = (uint32_t)(attackTimeSeconds * sampleRate);
        s_attackSamples.setTargetValue(attackSamples);
    }

    void setDecayTime(float seconds) {
        decayTimeSeconds = std::max(0.001f, seconds);
        decaySamples = (uint32_t)(decayTimeSeconds * sampleRate);
        s_decaySamples.setTargetValue(decaySamples);
    }

    void setSustainLevel(float level) {
        sustainLevelFloat = std::max(0.0f, std::min(1.0f, level));
        
        // DEBUG: Print sustain changes
        // printf("SUSTAIN: param=%.4f -> fix15=%d\n", sustainLevelFloat, (sustainLevelFloat <= 0.0001f) ? 0 : (int)float2fix15(sustainLevelFloat));
        
        // Force to exactly zero when parameter is zero (no tolerance needed)
        fix15 newSustainLevel = (sustainLevelFloat == 0.0f) ? FIX15_ZERO : float2fix15(sustainLevelFloat);
        s_sustainLevel.setTargetValue(newSustainLevel);
    }

    void setReleaseTime(float seconds) {
        releaseTimeSeconds = std::max(0.001f, seconds);
        releaseSamples = (uint32_t)(releaseTimeSeconds * sampleRate);
        s_releaseSamples.setTargetValue(releaseSamples);
    }

    void process(choc::buffer::InterleavedView<fix15>& buffer) override {
        auto numFrames = buffer.getNumFrames();
        auto numChannels = buffer.getNumChannels();

        if (state == State::Idle && currentLevel == FIX15_ZERO) {
            // OPTIMIZED: Use fixed-point zero instead of float applyGain
            buffer.clear();
            return;
        }

        for (uint32_t f = 0; f < numFrames; ++f) {
            fix15 envLevel = getNextValue();
            
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                buffer.getSample(ch, f) = multfix15(buffer.getSample(ch, f), envLevel);
            }
        }
    }

    fix15 getNextValue() {
        // Update smoothed sustain level and timing parameters
        sustainLevel = s_sustainLevel.getNextValue();
        
        // Get smoothed sample counts (no float multiplication needed)
        uint32_t currentAttackSamples = s_attackSamples.getNextValue();
        uint32_t currentDecaySamples = s_decaySamples.getNextValue();
        uint32_t currentReleaseSamples = s_releaseSamples.getNextValue();

        switch (state) {
            case State::StealFade:
                if (stealFadeSamples > 0) {
                    // OVERFLOW FIX: Use 32-bit division to handle large sample counts
                    fix15 progress;
                    if (sampleCounter >= stealFadeSamples) {
                        progress = FIX15_ONE;
                    } else {
                        // Use 64-bit arithmetic for consistency (though overflow unlikely with 5ms fade)
                        progress = (fix15)((uint64_t)sampleCounter * 32768 / stealFadeSamples);
                    }
                    
                    // Fade from start level to 0: level = startLevel * (1.0 - progress)
                    fix15 fade_factor = FIX15_ONE - progress;
                    currentLevel = multfix15(stealFadeStartLevel, fade_factor);
                    
                    sampleCounter++;
                    if (sampleCounter >= stealFadeSamples) {
                        // Fade complete, start attack for new note
                        currentLevel = FIX15_ZERO;
                        state = State::Attack;
                        sampleCounter = 0;
                    }
                } else {
                    // Instant steal (shouldn't happen with 5ms fade)
                    currentLevel = FIX15_ZERO;
                    state = State::Attack;
                    sampleCounter = 0;
                }
                break;
                
            case State::Attack:
                if (currentAttackSamples > 0) {
                    // OVERFLOW FIX: Use 32-bit division to handle large sample counts
                    fix15 progress;
                    if (sampleCounter >= currentAttackSamples) {
                        progress = FIX15_ONE;
                    } else {
                        // OVERFLOW FIX: Use 64-bit arithmetic to prevent overflow with large sample counts
                        progress = (fix15)((uint64_t)sampleCounter * 32768 / currentAttackSamples);
                    }
                    
                    // SMOOTH PARAMETER CHANGES: When timing changes, preserve current level
                    if (progress > FIX15_ONE) {
                        // Parameter change caused overshoot - preserve current level and recalc counter
                        if (currentLevel < FIX15_ONE) {
                            // Recalculate sample counter based on current level to maintain continuity
                            sampleCounter = (uint32_t)((uint64_t)currentLevel * currentAttackSamples >> 15);
                        }
                        progress = currentLevel; // Use current level instead of calculated progress
                    }
                    currentLevel = progress; // Direct assignment - no float conversion!
                    
                    sampleCounter++;
                    if (sampleCounter >= currentAttackSamples) {
                        currentLevel = FIX15_ONE;
                        state = State::Decay;
                        sampleCounter = 0; // Reset for decay phase
                    }
                } else {
                    // Instant attack
                    currentLevel = FIX15_ONE;
                    state = State::Decay;
                    sampleCounter = 0;
                }
                break;
                
            case State::Decay:
                if (currentDecaySamples > 0) {
                    // OVERFLOW FIX: Use 32-bit division to handle large sample counts
                    fix15 progress;
                    if (sampleCounter >= currentDecaySamples) {
                        progress = FIX15_ONE;
                    } else {
                        // OVERFLOW FIX: Use 64-bit arithmetic to prevent overflow with large sample counts
                        progress = (fix15)((uint64_t)sampleCounter * 32768 / currentDecaySamples);
                    }
                    
                    // SMOOTH PARAMETER CHANGES: When timing changes, preserve current level
                    if (progress > FIX15_ONE) {
                        // Parameter change caused overshoot - preserve current level and recalc counter
                        fix15 decay_range = FIX15_ONE - sustainLevel;
                        if (decay_range > 0 && currentLevel > sustainLevel && currentLevel <= FIX15_ONE) {
                            // Reverse calculate where we should be: progress = (1.0 - currentLevel) / decay_range
                            uint32_t reverse_progress = ((uint32_t)(FIX15_ONE - currentLevel) * 32768) / decay_range;
                            if (reverse_progress <= 32768) {
                                sampleCounter = (uint32_t)((uint64_t)reverse_progress * currentDecaySamples >> 15);
                            }
                        }
                        // Don't recalculate level - just continue with current level
                    } else {
                        // Normal case - calculate new level from progress
                        fix15 decay_range = FIX15_ONE - sustainLevel;
                        fix15 decay_amount = multfix15(progress, decay_range);
                        currentLevel = FIX15_ONE - decay_amount;
                    }
                    
                    sampleCounter++;
                    if (sampleCounter >= currentDecaySamples) {
                        currentLevel = sustainLevel;
                        state = State::Sustain;
                    }
                } else {
                    // Instant decay
                    currentLevel = sustainLevel;
                    state = State::Sustain;
                }
                break;
                
            case State::Sustain:
                // Smoothly track sustain level changes
                currentLevel = sustainLevel;
                // Force to true silence if sustain target is zero
                if (s_sustainLevel.getTargetValue() == FIX15_ZERO) {
                    currentLevel = FIX15_ZERO;
                }
                break;
                
            case State::Release:
                if (currentReleaseSamples > 0) {
                    // OVERFLOW FIX: Use 32-bit division to handle large sample counts
                    fix15 progress;
                    if (sampleCounter >= currentReleaseSamples) {
                        progress = FIX15_ONE;
                    } else {
                        // OVERFLOW FIX: Use 64-bit arithmetic to prevent overflow with large sample counts
                        progress = (fix15)((uint64_t)sampleCounter * 32768 / currentReleaseSamples);
                    }
                    
                    // SMOOTH PARAMETER CHANGES: When timing changes, preserve current level
                    if (progress > FIX15_ONE) {
                        // Parameter change caused overshoot - preserve current level and recalc counter
                        if (releaseStartLevel > 0 && currentLevel >= 0 && currentLevel <= releaseStartLevel) {
                            // Reverse calculate: progress = 1.0 - (currentLevel / releaseStartLevel)
                            uint32_t level_ratio = ((uint32_t)currentLevel * 32768) / releaseStartLevel;
                            if (level_ratio <= 32768) {
                                uint32_t reverse_progress = 32768 - level_ratio;
                                sampleCounter = (uint32_t)((uint64_t)reverse_progress * currentReleaseSamples >> 15);
                            }
                        }
                        // Don't recalculate level - just continue with current level
                    } else {
                        // Normal case - calculate new level from progress
                        fix15 fade_factor = FIX15_ONE - progress;
                        currentLevel = multfix15(releaseStartLevel, fade_factor);
                    }
                    
                    sampleCounter++;
                    if (sampleCounter >= currentReleaseSamples) {
                        // FINAL FIX: Ensure clean transition to idle state
                        currentLevel = FIX15_ZERO;
                        state = State::Idle;
                        sampleCounter = 0; // Reset counter to prevent any weirdness
                    }
                } else {
                    // Instant release
                    currentLevel = FIX15_ZERO;
                    state = State::Idle;
                    sampleCounter = 0; // Ensure clean state
                }
                break;
                
            case State::Idle:
            default:
                currentLevel = FIX15_ZERO;
                break;
        }

        return currentLevel;
    }

private:
    void updateSampleCounts() {
        attackSamples = (uint32_t)(attackTimeSeconds * sampleRate);
        decaySamples = (uint32_t)(decayTimeSeconds * sampleRate);
        releaseSamples = (uint32_t)(releaseTimeSeconds * sampleRate);
    }

    float sampleRate;
    State state = State::Idle;
    fix15 currentLevel = FIX15_ZERO;
    fix15 sustainLevel = float2fix15(0.7f);
    Fix15SmoothedValue s_sustainLevel;
    
    // Smoothed timing parameters as sample counts (eliminates float multiplication per sample)
    SmoothedValue<uint32_t> s_attackSamples;
    SmoothedValue<uint32_t> s_decaySamples; 
    SmoothedValue<uint32_t> s_releaseSamples;
    
    // 32-bit sample counting approach - handles long envelopes without overflow
    uint32_t sampleCounter = 0;        // Current sample count in current envelope phase
    uint32_t attackSamples = 441;      // 0.01s at 44.1kHz
    uint32_t decaySamples = 8820;      // 0.2s at 44.1kHz  
    uint32_t releaseSamples = 22050;   // 0.5s at 44.1kHz
    fix15 releaseStartLevel = FIX15_ZERO; // Level when release started
    
    // Voice stealing fade parameters
    float stealFadeTimeSeconds = 0.005f;  // 5ms steal fade
    uint32_t stealFadeSamples = 220;       // 0.005s at 44.1kHz
    fix15 stealFadeStartLevel = FIX15_ZERO; // Level when steal fade started
    
    float attackTimeSeconds = 0.01f;
    float decayTimeSeconds = 0.2f;
    float sustainLevelFloat = 0.7f;
    float releaseTimeSeconds = 0.5f;
};