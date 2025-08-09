/**
 * ParameterStore.h - Global Parameter Management System
 *
 * Provides centralized storage and initialization for all synthesizer
 * parameters. Uses a global vector as the "single source of truth" for
 * parameter access across different threads and modules.
 *
 * Design Principles:
 * - Single global store prevents parameter duplication
 * - Initialization happens once at startup before audio processing begins
 * - Thread-safe: Parameter objects use atomic storage internally
 * - Lookup by string ID allows flexible parameter binding
 * - MIDI CC numbers integrated for hardware control
 */

#pragma once
#include "Parameter.h"
#include <vector>

/**
 * Global parameter storage - single source of truth for all synth parameters
 *
 * This vector contains pointers to all Parameter objects in the system.
 * Accessed by:
 * - Audio modules for parameter value reading
 * - MIDI/UI systems for parameter updates
 * - Rotary encoder system for parameter selection/editing
 *
 * Thread Safety: The vector itself is read-only after initialization.
 * Individual Parameter objects handle thread-safe value updates.
 */
inline std::vector<Parameter *> g_synth_parameters;

/**
 * Initialize all synthesizer parameters in the global store
 *
 * CRITICAL: Must be called exactly once at system startup,
 * BEFORE launching the audio thread or any parameter access.
 *
 * Parameter Design:
 * - Physical ranges chosen for musical usefulness
 * - Default values provide reasonable starting sounds
 * - MIDI CC numbers assigned for standard hardware controllers
 * - String IDs match audio module expectations
 */
inline void initialize_parameters() {
  // Clear any previous parameters to be safe (for re-initialization)
  for (auto *p : g_synth_parameters)
    delete p;
  g_synth_parameters.clear();

  // === ADSR Envelope Parameters ===
  g_synth_parameters.push_back(new Parameter(
      "attack", "Attack", 0.001f, 2.5f, 0.01f, 74)); // Attack time (seconds)
  g_synth_parameters.push_back(new Parameter("decay", "Decay", 0.003f, 2.0f,
                                             0.2f, 71)); // Decay time (seconds)
  g_synth_parameters.push_back(new Parameter("sustain", "Sustain", 0.0f, 1.0f,
                                             0.3f, 73)); // Sustain level (0-1)
  g_synth_parameters.push_back(new Parameter(
      "release", "Release", 0.01f, 5.0f, 0.1f, 72)); // Release time (seconds)

  // === Oscillator Mix Parameters ===
  g_synth_parameters.push_back(
      new Parameter("sawLevel", "Saw Level", 0.0f, 1.0f, 1.0f, 79)); // Saw oscillator mix level
  g_synth_parameters.push_back(
      new Parameter("pulseLevel", "Pulse Level", 0.0f, 1.0f, 0.0f, 80)); // Pulse oscillator mix level
  g_synth_parameters.push_back(
      new Parameter("subLevel", "Sub Level", 0.0f, 1.0f, 0.0f, 82)); // Sub oscillator mix level
  g_synth_parameters.push_back(
      new Parameter("noiseLevel", "Noise Level", 0.0f, 1.0f, 0.0f, 78)); // Noise oscillator mix level
  
  // === Oscillator Shape Parameters ===
  g_synth_parameters.push_back(
      new Parameter("pulseWidth", "Pulse Width", 0.05f, 0.95f, 0.5f, 81)); // Pulse width (duty cycle)

  // === Filter Parameters ===
  g_synth_parameters.push_back(
      new Parameter("filterCutoff", "Cutoff", 0.0f, 1.0f, 0.5f, 76));
  g_synth_parameters.push_back(
      new Parameter("filterResonance", "Resonance", 0.0f, 1.0f, 0.2f, 77));

  // === Master Controls ===
  g_synth_parameters.push_back(new Parameter("masterVol", "Master Volume", 0.0f,
                                             1.0f, 0.05f,
                                             75)); // Overall output level
}