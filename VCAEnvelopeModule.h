#pragma once

#include "AudioModule.h"
#include <cmath>     // For std::max, std::min
#include <algorithm> // For std::max, std::min on some compilers

class VCAEnvelopeModule : public AudioModule {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    VCAEnvelopeModule(double sampleRate)
        : sampleRate(sampleRate) {
        setAttackTime(0.01);
        setDecayTime(0.2);
        setSustainLevel(0.7);
        setReleaseTime(0.5);
    }

    //==============================================================================
    // Public Control Methods
    //==============================================================================
    void noteOn() {
        if (currentState == State::Release) {
             recalculateAttackIncrementFromCurrentLevel();
        }
        currentState = State::Attack;
    }

    void noteOff() {
        if (currentState != State::Idle && currentState != State::Release) {
            currentState = State::Release;
            recalculateReleaseIncrement();
        }
    }

    bool isActive() const {
        return currentState != State::Idle;
    }

    //==============================================================================
    // Parameter Setters
    //==============================================================================
    void setAttackTime(double seconds) {
        attackTimeSeconds = std::max(0.001, seconds);
        recalculateAttackIncrement();
    }

    void setDecayTime(double seconds) {
        decayTimeSeconds = std::max(0.001, seconds);
        recalculateDecayIncrement();
    }

    void setSustainLevel(double level) {
        sustainLevel = std::max(0.0, std::min(1.0, level));
        recalculateDecayIncrement();
    }

    void setReleaseTime(double seconds) {
        releaseTimeSeconds = std::max(0.001, seconds);
    }

    //==============================================================================
    // Parameter Getters (NEWLY ADDED)
    //==============================================================================
    double getAttackTime() const { return attackTimeSeconds; }
    double getDecayTime() const { return decayTimeSeconds; }
    double getSustainLevel() const { return sustainLevel; }
    double getReleaseTime() const { return releaseTimeSeconds; }

    //==============================================================================
    // Core Processing
    //==============================================================================
    void process(choc::buffer::InterleavedView<float> buffer) override {
        auto numFrames = buffer.getNumFrames();
        auto numChannels = buffer.getNumChannels();

        if (currentState == State::Idle && currentLevel == 0.0) {
            buffer.clear();
            return;
        }

        for (uint32_t f = 0; f < numFrames; ++f) {
            switch (currentState) {
                case State::Attack:
                    currentLevel += attackIncrement;
                    if (currentLevel >= 1.0) {
                        currentLevel = 1.0;
                        currentState = State::Decay;
                        recalculateAttackIncrement();
                    }
                    break;
                case State::Decay:
                    currentLevel -= decayIncrement;
                    if (currentLevel <= sustainLevel) {
                        currentLevel = sustainLevel;
                        currentState = State::Sustain;
                    }
                    break;
                case State::Sustain:
                    break;
                case State::Release:
                    currentLevel -= releaseIncrement;
                    if (currentLevel <= 0.0) {
                        currentLevel = 0.0;
                        currentState = State::Idle;
                    }
                    break;
                case State::Idle:
                default:
                    currentLevel = 0.0;
                    break;
            }

            const float gain = static_cast<float>(currentLevel);
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                buffer.getSample(ch, f) *= gain;
            }
        }
    }

private:
    void recalculateAttackIncrement() { attackIncrement = 1.0 / (attackTimeSeconds * sampleRate); }
    void recalculateAttackIncrementFromCurrentLevel() {
        double remainingRise = 1.0 - currentLevel;
        if (remainingRise > 0) { attackIncrement = remainingRise / (attackTimeSeconds * sampleRate); }
        else { recalculateAttackIncrement(); }
    }
    void recalculateDecayIncrement() { decayIncrement = (1.0 - sustainLevel) / (decayTimeSeconds * sampleRate); }
    void recalculateReleaseIncrement() { releaseIncrement = currentLevel / (releaseTimeSeconds * sampleRate); }

    double sampleRate;
    State currentState = State::Idle;
    double attackTimeSeconds, decayTimeSeconds, sustainLevel, releaseTimeSeconds;
    double attackIncrement, decayIncrement, releaseIncrement;
    double currentLevel = 0.0;
};