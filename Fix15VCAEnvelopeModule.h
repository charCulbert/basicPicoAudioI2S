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
 * Fixed-point ADSR envelope generator with VCA functionality
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
        updateSampleCounts();
    }

    void noteOn() {
        currentLevel = FIX15_ZERO;
        state = State::Attack;
        samplesInCurrentStage = 0;
    }

    void noteOff() {
        if (state != State::Idle) {
            releaseStartLevel = currentLevel; // Remember level when release started
            state = State::Release;
            samplesInCurrentStage = 0;
        }
    }

    bool isActive() const { return state != State::Idle; }

    void setAttackTime(float seconds) {
        attackTimeSeconds = std::max(0.001f, seconds);
        updateSampleCounts();
    }

    void setDecayTime(float seconds) {
        decayTimeSeconds = std::max(0.001f, seconds);
        updateSampleCounts();
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
        updateSampleCounts();
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
        // Update smoothed sustain level
        sustainLevel = s_sustainLevel.getNextValue();

        switch (state) {
            case State::Attack:
                if (attackSamples > 0) {
                    // Calculate progress using pure fix15: samplesInCurrentStage / attackSamples
                    currentLevel = divfix15(int2fix15(samplesInCurrentStage), int2fix15(attackSamples));
                    if (samplesInCurrentStage >= attackSamples) {
                        currentLevel = int2fix15(1);
                        state = State::Decay;
                        samplesInCurrentStage = 0;
                    } else {
                        samplesInCurrentStage++;
                    }
                } else {
                    // Instant attack
                    currentLevel = int2fix15(1);
                    state = State::Decay;
                    samplesInCurrentStage = 0;
                }
                break;
                
            case State::Decay:
                if (decaySamples > 0) {
                    // Calculate progress using pure fix15: samplesInCurrentStage / decaySamples
                    fix15 progress = divfix15(int2fix15(samplesInCurrentStage), int2fix15(decaySamples));
                    // level = 1.0 - progress * (1.0 - sustainLevel) = 1 - progress + progress * sustainLevel
                    fix15 one = int2fix15(1);
                    currentLevel = one - progress + multfix15(progress, sustainLevel);
                    if (samplesInCurrentStage >= decaySamples || currentLevel <= sustainLevel) {
                        currentLevel = sustainLevel;
                        state = State::Sustain;
                    } else {
                        samplesInCurrentStage++;
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
                // Force to true silence if sustain target is zero (fix15 smoother may never reach exactly zero)
                if (s_sustainLevel.getTargetValue() == FIX15_ZERO) {
                    currentLevel = FIX15_ZERO;
                }
                break;
                
            case State::Release:
                if (releaseSamples > 0) {
                    // Calculate progress using pure fix15: samplesInCurrentStage / releaseSamples
                    fix15 progress = divfix15(int2fix15(samplesInCurrentStage), int2fix15(releaseSamples));
                    // level = releaseStartLevel * (1.0 - progress)
                    fix15 one = int2fix15(1);
                    currentLevel = multfix15(releaseStartLevel, (one - progress));
                    if (samplesInCurrentStage >= releaseSamples || currentLevel <= FIX15_ZERO) {
                        currentLevel = FIX15_ZERO;
                        state = State::Idle;
                    } else {
                        samplesInCurrentStage++;
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
        attackSamples = (int)(attackTimeSeconds * sampleRate);
        decaySamples = (int)(decayTimeSeconds * sampleRate);
        releaseSamples = (int)(releaseTimeSeconds * sampleRate);
    }

    float sampleRate;
    State state = State::Idle;
    fix15 currentLevel = FIX15_ZERO;
    fix15 sustainLevel = float2fix15(0.7f);
    Fix15SmoothedValue s_sustainLevel;
    
    // Counter-based approach
    int samplesInCurrentStage = 0;
    int attackSamples = 441;      // 0.01s at 44.1kHz
    int decaySamples = 8820;      // 0.2s at 44.1kHz  
    int releaseSamples = 22050;   // 0.5s at 44.1kHz
    fix15 releaseStartLevel = FIX15_ZERO; // Level when release started
    
    float attackTimeSeconds = 0.01f;
    float decayTimeSeconds = 0.2f;
    float sustainLevelFloat = 0.7f;
    float releaseTimeSeconds = 0.5f;
};