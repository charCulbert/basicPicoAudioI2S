#pragma once

#include "AudioModule.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include <vector>
#include <array>
#include <numeric>

//==============================================================================
// <<< CHANGED: This is a heavily optimized, mono-core version of the reverb.
class SimpleReverbModule : public AudioModule {
private:
    // Filter classes are unchanged, just used less.
    class DampingFilter { public: void setDamping(float v) { damping = v; } float process(float i) { store=(i*(1.0f-damping))+(store*damping); return store; } private: float store=0.0f, damping=0.5f; };
    class CombFilter { public: void setBuffer(std::vector<float>& b) { buffer=&b; b.assign(b.size(),0.0f); index=0; } void setFeedback(float v) { feedback=v; } void setDamping(float v) { d.setDamping(v); } float process(float i) { float o=(*buffer)[index]; float f=d.process(o); (*buffer)[index]=i+(f*feedback); if(++index>=buffer->size()){index=0;} return o; } private: std::vector<float>* buffer=nullptr; DampingFilter d; float feedback=0.0f; int index=0; };
    class AllPassFilter { public: void setBuffer(std::vector<float>& b) { buffer=&b; b.assign(b.size(),0.0f); index=0; } void setFeedback(float v) { feedback=v; } float process(float i) { float d=(*buffer)[index]; float o=d-i; (*buffer)[index]=i+(d*feedback); if(++index>=buffer->size()){index=0;} return o; } private: std::vector<float>* buffer=nullptr; float feedback=0.5f; int index=0; };

public:
    SimpleReverbModule(float sample_rate) {
        for (auto* p : g_synth_parameters) {
            if (p->getID() == "reverbSize") p_room_size = p;
            if (p->getID() == "reverbDamp") p_damping = p;
            if (p->getID() == "reverbMix")  p_mix = p;
        }
        s_room_size.reset(sample_rate, 0.05);
        s_damping.reset(sample_rate, 0.05);
        s_mix.reset(sample_rate, 0.05);
        s_room_size.setValue(p_room_size->getValue());
        s_damping.setValue(p_damping->getValue());
        s_mix.setValue(p_mix->getValue());

        // <<< CHANGED: Reduced number of filters and their delay lengths
        constexpr std::array<int, numCombs> comb_tunings = {1116, 1277, 1491};
        constexpr std::array<int, numAllPass> allpass_tunings = {225, 441};
        constexpr int stereoizer_tuning = 131; // A short delay for the stereoizer

        for (int i=0; i<numCombs; ++i) {
            comb_buffers[i].resize(comb_tunings[i]);
            comb_filters[i].setBuffer(comb_buffers[i]);
        }
        for (int i=0; i<numAllPass; ++i) {
            allpass_buffers[i].resize(allpass_tunings[i]);
            allpass_filters[i].setBuffer(allpass_buffers[i]);
            allpass_filters[i].setFeedback(0.5f);
        }

        // <<< NEW: Initialize the lightweight stereoizer filter
        stereoizer_buffer.resize(stereoizer_tuning);
        stereoizer.setBuffer(stereoizer_buffer);
        stereoizer.setFeedback(0.5f);
    }

    void process(choc::buffer::InterleavedView<float>& buffer) override {
        update_parameters();

        if (buffer.getNumChannels() < 2) return;

        for (uint32_t f = 0; f < buffer.getNumFrames(); ++f) {
            if (control_rate_counter == 0) {
                float currentRoomSize = s_room_size.getCurrentValue();
                float currentDamping = s_damping.getCurrentValue();
                for (int i = 0; i < numCombs; ++i) {
                    comb_filters[i].setFeedback(currentRoomSize);
                    comb_filters[i].setDamping(currentDamping);
                }
            }
            if (++control_rate_counter >= CONTROL_RATE_INTERVAL) {
                control_rate_counter = 0;
            }

            s_room_size.getNextValue();
            s_damping.getNextValue();
            float wetLevel = s_mix.getNextValue();
            float dryLevel = 1.0f - wetLevel;

            float inL = buffer.getSample(0, f);
            float inR = buffer.getSample(1, f);

            // <<< CHANGED: Core reverb processing is now mono
            float input_mono = (inL + inR) * 0.5f;
            float wet_mono = 0.0f;

            // Process through parallel comb filters
            for (int i = 0; i < numCombs; ++i) {
                wet_mono += comb_filters[i].process(input_mono);
            }
            // Process through series all-pass filters
            for (int i = 0; i < numAllPass; ++i) {
                wet_mono = allpass_filters[i].process(wet_mono);
            }

            // <<< CHANGED: Lightweight pseudo-stereo generation
            float outL = wet_mono;
            float outR = stereoizer.process(wet_mono); // Decorrelate the right channel

            // Mix wet and dry signals
            buffer.getSample(0, f) = (outL * wetLevel * 0.2f) + (inL * dryLevel);
            buffer.getSample(1, f) = (outR * wetLevel * 0.2f) + (inR * dryLevel);
        }
    }

private:
    void update_parameters() {
        s_room_size.setTargetValue(p_room_size->getValue());
        s_damping.setTargetValue(p_damping->getValue());
        s_mix.setTargetValue(p_mix->getValue());
    }

    // <<< CHANGED: Much higher interval is safer now
    static constexpr int CONTROL_RATE_INTERVAL = 64;
    int control_rate_counter = 0;

    // <<< CHANGED: Fewer filters and single arrays (not L/R)
    static constexpr int numCombs = 3;
    static constexpr int numAllPass = 2;

    Parameter *p_room_size, *p_damping, *p_mix;
    SmoothedValue<float> s_room_size, s_damping, s_mix;

    // Core mono filter banks
    std::array<CombFilter, numCombs> comb_filters;
    std::array<AllPassFilter, numAllPass> allpass_filters;
    std::array<std::vector<float>, numCombs> comb_buffers;
    std::array<std::vector<float>, numAllPass> allpass_buffers;

    // <<< NEW: Lightweight stereoizer filter
    AllPassFilter stereoizer;
    std::vector<float> stereoizer_buffer;
};