#pragma once
#include <vector>
#include "Parameter.h"

// A single, global list containing all synth parameters.
// This is the "single source of truth".
inline std::vector<Parameter*> g_synth_parameters;

// A helper function to create all our parameters in one place.
inline void initialize_parameters() {
    // Clear any previous parameters to be safe
    for (auto* p : g_synth_parameters) delete p;
    g_synth_parameters.clear();

    // Create all the parameters for our synth
    g_synth_parameters.push_back(new Parameter("modIndex",    "Mod Index",    0.0f,   10.0f,  2.0f,   1));
    g_synth_parameters.push_back(new Parameter("harmonicity", "Harmonicity",  0.5f,   10.0f,   6.0f,   10));
    g_synth_parameters.push_back(new Parameter("attack",      "Attack",       0.001f, 2.0f,   0.0f,  74));
    g_synth_parameters.push_back(new Parameter("decay",       "Decay",        0.001f, 3.0f,   0.07f,   71));
    g_synth_parameters.push_back(new Parameter("sustain",     "Sustain",      0.0f,   1.0f,   0.35f,   73));
    g_synth_parameters.push_back(new Parameter("release",     "Release",      0.01f,  5.0f,   0.2f,   72));
    g_synth_parameters.push_back(new Parameter("masterVol",   "Master Volume",0.0f,   1.0f,   0.08f,   75));
}