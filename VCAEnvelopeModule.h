#pragma once

#include "AudioModule.h"
#include "choc/audio/choc_SampleBuffers.h"
#include <cmath>
#include <algorithm>

class VCAEnvelopeModule : public AudioModule {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release, StealFade };

    VCAEnvelopeModule(double sampleRate) : sampleRate(sampleRate) {
        setAttackTime(0.01);
        setDecayTime(0.2);
        setSustainLevel(0.7);
        setReleaseTime(0.5);

        // Very short fade time to avoid clicks on note steal (1ms)
        stealFadeTimeSeconds = 0.005;
        recalculateStealFadeIncrement();
    }

    void noteOn() {
        if (currentLevel > 0.0) {
            // Voice is being stolen - fade down quickly first
            currentState = State::StealFade;
            stealFadeStartLevel = currentLevel;
            recalculateStealFadeIncrement();
        } else {
            // Voice is free - start attack immediately
            currentLevel = 0.0;
            currentState = State::Attack;
            recalculateAttackIncrement();
        }
    }

    void noteOff() {
        if (currentState != State::Idle) {
            currentState = State::Release;
            recalculateReleaseIncrement();
        }
    }

    bool isActive() const { return currentState != State::Idle; }

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

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        auto numFrames = buffer.getNumFrames();
        auto numChannels = buffer.getNumChannels();

        if (currentState == State::Idle && currentLevel == 0.0) {
            choc::buffer::applyGain(buffer, 0.0f);
            return;
        }

        for (uint32_t f = 0; f < numFrames; ++f) {
            switch (currentState) {
                case State::StealFade:
                    currentLevel -= stealFadeIncrement;
                    if (currentLevel <= 0.0) {
                        currentLevel = 0.0;
                        currentState = State::Attack;
                        recalculateAttackIncrement();
                    }
                    break;
                case State::Attack:
                    currentLevel += attackIncrement;
                    if (currentLevel >= 1.0) {
                        currentLevel = 1.0;
                        currentState = State::Decay;
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
    void recalculateAttackIncrement() { attackIncrement = (attackTimeSeconds > 0.0) ? (1.0 / (attackTimeSeconds * sampleRate)) : 1.0; }
    void recalculateDecayIncrement() { decayIncrement = (decayTimeSeconds > 0.0) ? ((1.0 - sustainLevel) / (decayTimeSeconds * sampleRate)) : 1.0; }
    void recalculateReleaseIncrement() { releaseIncrement = (releaseTimeSeconds > 0.0 && currentLevel > 0.0) ? (currentLevel / (releaseTimeSeconds * sampleRate)) : currentLevel; }
    void recalculateStealFadeIncrement() { stealFadeIncrement = (stealFadeTimeSeconds > 0.0 && stealFadeStartLevel > 0.0) ? (stealFadeStartLevel / (stealFadeTimeSeconds * sampleRate)) : stealFadeStartLevel; }

    double sampleRate;
    State currentState = State::Idle;
    double attackTimeSeconds, decayTimeSeconds, sustainLevel, releaseTimeSeconds;
    double attackIncrement = 0.0, decayIncrement = 0.0, releaseIncrement = 0.0;
    double currentLevel = 0.0;

    // Note stealing fade parameters
    double stealFadeTimeSeconds;
    double stealFadeIncrement = 0.0;
    double stealFadeStartLevel = 0.0;
};