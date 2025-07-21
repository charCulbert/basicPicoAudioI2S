#pragma once

#include "AudioModule.h"
#include "Fix15.h"
#include "ParameterStore.h"

class GainModule : public AudioModule {
public:
  GainModule(float sampleRate) {
    // Find master volume parameter
    for (auto *p : g_synth_parameters) {
      if (p->getID() == "masterVol")
        p_master_vol = p;
    }
  }

  void process(choc::buffer::InterleavedView<fix15> &buffer) override {
    if (!p_master_vol) return;
    
    float vol = p_master_vol->getValue();
    
    // True silence when parameter is zero
    if (vol == 0.0f) {
      buffer.clear();
      return;
    }
    
    // Convert to fix15 once - use bit shift for efficiency
    fix15 gain = (fix15)(vol * 32768.0f);

    for (uint32_t frame = 0; frame < buffer.getNumFrames(); ++frame) {
      for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
        fix15 sample = buffer.getSample(ch, frame);
        buffer.getSample(ch, frame) = multfix15(sample, gain);
      }
    }
  }

private:
  Parameter *p_master_vol = nullptr;
};