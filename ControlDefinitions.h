#pragma once

#include <vector>
#include <functional>
#include <cstdio> // For printf in the lambda functions if needed
#include "freqModSineModule.h"
#include "VCAEnvelopeModule.h"

// Define the structure that holds EVERYTHING about a single control pair (Up/Down).
struct ControlDefinition {
    const char* label;          // Name for the help text, e.g., "Attack Time"
    char key_down;              // The key to decrease the value
    char key_up;                // The key to increase the value

    // The actual C++ code to run for the 'down' and 'up' commands.
    // This replaces the giant switch statement.
    std::function<void(FreqModSineModule*, VCAEnvelopeModule*)> action_down;
    std::function<void(FreqModSineModule*, VCAEnvelopeModule*)> action_up;
};

// =================================================================================
// === THE SINGLE SOURCE OF TRUTH ===
// =================================================================================
//
// By declaring this global vector "inline", we can define it entirely in this
// header file without causing linker errors.
//
// To add or change a control, you ONLY need to edit this list!
//
inline const std::vector<ControlDefinition> g_controlDefinitions = {
    // --- Synth Voice Controls ---
    {
        "Note", 'a', 's',
        [](auto* osc, auto* env) { env->noteOff(); },
        [](auto* osc, auto* env) { env->noteOn(); }
    },
    // --- Oscillator Controls ---
    {
        "Modulation Index", 'd', 'f',
        [](auto* osc, auto* env) { osc->setModulationIndex(osc->getModulationIndex() - 0.2); },
        [](auto* osc, auto* env) { osc->setModulationIndex(osc->getModulationIndex() + 0.2); }
    },
    {
        "Harmonicity", 'g', 'h',
        [](auto* osc, auto* env) { osc->setHarmonicityRatio(osc->getHarmonicityRatio() - 0.1); },
        [](auto* osc, auto* env) { osc->setHarmonicityRatio(osc->getHarmonicityRatio() + 0.1); }
    },
    // --- Envelope Controls ---
    {
        "Attack Time", 'q', 'w',
        [](auto* osc, auto* env) { env->setAttackTime(env->getAttackTime() - 0.05); },
        [](auto* osc, auto* env) { env->setAttackTime(env->getAttackTime() + 0.05); }
    },
    {
        "Decay Time", 'e', 'r',
        [](auto* osc, auto* env) { env->setDecayTime(env->getDecayTime() - 0.05); },
        [](auto* osc, auto* env) { env->setDecayTime(env->getDecayTime() + 0.05); }
    },
    {
        "Sustain Level", 't', 'y',
        [](auto* osc, auto* env) { env->setSustainLevel(env->getSustainLevel() - 0.05); },
        [](auto* osc, auto* env) { env->setSustainLevel(env->getSustainLevel() + 0.05); }
    },
    {
        "Release Time", 'u', 'i',
        [](auto* osc, auto* env) { env->setReleaseTime(env->getReleaseTime() - 0.1); },
        [](auto* osc, auto* env) { env->setReleaseTime(env->getReleaseTime() + 0.1); }
    }
};