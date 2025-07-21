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
    enum class State { Idle, Attack, Decay, Sustain, Release };

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
        
        updateSampleCounts();
    }

    void noteOn() {
        currentLevel = FIX15_ZERO;
        state = State::Attack;
        sampleCounter = 0;
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
            choc::buffer::applyGain(buffer, 0.0f);
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
            case State::Attack:
                if (currentAttackSamples > 0) {
                    // Use 32-bit sample counting with floating-point progress calculation
                    float progress = (float)sampleCounter / (float)currentAttackSamples;
                    progress = std::min(progress, 1.0f); // Clamp to prevent overshoot
                    currentLevel = float2fix15(progress);
                    
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
                    // Use 32-bit sample counting with floating-point progress calculation
                    float progress = (float)sampleCounter / (float)currentDecaySamples;
                    progress = std::min(progress, 1.0f); // Clamp to prevent overshoot
                    float sustainFloat = fix152float(sustainLevel);
                    // Interpolate from 1.0 down to sustain level
                    float levelFloat = 1.0f - (progress * (1.0f - sustainFloat));
                    currentLevel = float2fix15(levelFloat);
                    
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
                    // Use 32-bit sample counting with floating-point progress calculation
                    float progress = (float)sampleCounter / (float)currentReleaseSamples;
                    progress = std::min(progress, 1.0f); // Clamp to prevent overshoot
                    float releaseStartFloat = fix152float(releaseStartLevel);
                    // Interpolate from release start level down to 0
                    float levelFloat = releaseStartFloat * (1.0f - progress);
                    currentLevel = float2fix15(levelFloat);
                    
                    sampleCounter++;
                    if (sampleCounter >= currentReleaseSamples) {
                        currentLevel = FIX15_ZERO;
                        state = State::Idle;
                    }
                } else {
                    // Instant release
                    currentLevel = FIX15_ZERO;
                    state = State::Idle;
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
    
    float attackTimeSeconds = 0.01f;
    float decayTimeSeconds = 0.2f;
    float sustainLevelFloat = 0.7f;
    float releaseTimeSeconds = 0.5f;
};