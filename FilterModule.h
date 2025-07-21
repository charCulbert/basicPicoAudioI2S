#pragma once

#include "AudioModule.h"
#include "Fix15.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"

class FilterModule : public AudioModule {
public:
  FilterModule(float sampleRate) : sampleRate(sampleRate) {
    for (auto *p : g_synth_parameters) {
      if (p->getID() == "filterCutoff")
        p_cutoff = p;
      if (p->getID() == "filterResonance")
        p_resonance = p;
    }

    s_cutoff.reset(sampleRate, 0.02);
    s_resonance.reset(sampleRate, 0.02);

    if (p_cutoff)
      s_cutoff.setValue(p_cutoff->getValue());
    if (p_resonance)
      s_resonance.setValue(p_resonance->getValue());

    // Initialize filter states
    for (int ch = 0; ch < 2; ch++) {
      stage1[ch] = stage2[ch] = stage3[ch] = stage4[ch] = 0;
    }
  }

  void process(choc::buffer::InterleavedView<fix15> &buffer) override {
    update_parameters();

    for (uint32_t frame = 0; frame < buffer.getNumFrames(); ++frame) {
      fix15 cutoff_param = s_cutoff.getNextValue();
      fix15 resonance_param = s_resonance.getNextValue();

      // Map cutoff: 0-1 -> 0.001-0.85 (pre-computed constants)
      fix15 g = multfix15(cutoff_param, 27787) + 33;  // 0.849 * 32768 = 27787, 0.001 * 32768 = 33
      // Map resonance: 0-1 -> 0-3.9
      fix15 res = multfix15(resonance_param, 127795);  // 3.9 * 32768 = 127795

      for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
        fix15 input = buffer.getSample(ch, frame);

        // Moog ladder with resonance feedback
        fix15 fb_input = input - multfix15(res, stage4[ch]);

        // Clamp input
        if (fb_input > 524288) fb_input = 524288;      // 16.0 * 32768 = 524288
        else if (fb_input < -524288) fb_input = -524288;

        // 4-pole ladder: each stage is stage += g * (input - stage)
        fix15 temp = fb_input - stage1[ch];
        stage1[ch] += multfix15(g, temp);

        temp = stage1[ch] - stage2[ch];
        stage2[ch] += multfix15(g, temp);

        temp = stage2[ch] - stage3[ch];
        stage3[ch] += multfix15(g, temp);

        temp = stage3[ch] - stage4[ch];
        stage4[ch] += multfix15(g, temp);

        // Clamp filter output
        if (stage4[ch] > 262144) stage4[ch] = 262144;      // 8.0 * 32768 = 262144
        else if (stage4[ch] < -262144) stage4[ch] = -262144;

        // 2.5x makeup gain
        fix15 output = multfix15(stage4[ch], 81920);       // 2.5 * 32768 = 81920
        
        // Final clamp
        if (output > 524288) output = 524288;            // 16.0 * 32768 = 524288
        else if (output < -524288) output = -524288;

        buffer.getSample(ch, frame) = output;
      }
    }
  }

private:
  void update_parameters() {
    if (p_cutoff)
      s_cutoff.setTargetValue(p_cutoff->getValue());
    if (p_resonance)
      s_resonance.setTargetValue(p_resonance->getValue());
  }

  float sampleRate;
  Parameter *p_cutoff = nullptr;
  Parameter *p_resonance = nullptr;

  Fix15SmoothedValue s_cutoff;
  Fix15SmoothedValue s_resonance;

  // Moog ladder filter stages (4-pole, stereo)
  fix15 stage1[2];
  fix15 stage2[2];
  fix15 stage3[2];
  fix15 stage4[2];
};