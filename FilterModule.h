#pragma once

#include "AudioModule.h"
#include "Fix15.h"
#include "ParameterStore.h"
#include "SmoothedValue.h"
#include <cmath>

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
  }

  void process(choc::buffer::InterleavedView<fix15> &buffer) override {
    update_parameters();

    fix15 cutoff = s_cutoff.getNextValue();
    fix15 resonance = s_resonance.getNextValue();

    // Coefficients for State Variable Filter (SVF)
    // Mapped from 0-1 float to musically useful ranges.
    float cutoff_float = fix152float(s_cutoff.getNextValue());
    float resonance_float = fix152float(s_resonance.getNextValue());

    // A common mapping for cutoff control to filter coefficient 'f'
    // This gives an exponential response which is more musical.
    // The range is clamped to prevent instability.
    float f_float =
        2.0f *
        sinf(3.14159f * (20.0f * powf(1000.0f, cutoff_float)) / sampleRate);
    if (f_float > 1.0f)
      f_float = 1.0f;
    if (f_float < 0.0f)
      f_float = 0.0f;

    fix15 f = float2fix15(f_float);

    // 'q' controls resonance. Higher resonance parameter = lower damping.
    // We map resonance to a Q factor, then convert to damping.
    // This prevents self-oscillation at high resonance settings.
    float Q =
        0.5f + resonance_float * 20.0f; // Map resonance to Q from 0.5 to 20.5
    fix15 q = float2fix15(1.0f / Q);

    for (uint32_t f_idx = 0; f_idx < buffer.getNumFrames(); ++f_idx) {
      for (uint32_t ch = 0; ch < buffer.getNumChannels(); ++ch) {
        fix15 in = buffer.getSample(ch, f_idx);

        fix15 high = in - multfix15(q, z1[ch]) - z2[ch];
        fix15 band = multfix15(f, high) + z1[ch];
        fix15 low = multfix15(f, band) + z2[ch];

        z1[ch] = band;
        z2[ch] = low;

        buffer.getSample(ch, f_idx) = low;
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

  fix15 z1[2] = {0, 0};
  fix15 z2[2] = {0, 0};
};
