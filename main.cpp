#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <cstdint>
// --- Core Application Headers ---
#include "AudioEngine.h"
#include "I2sAudioOutput.h"
#include "ParameterStore.h"
#include "GainModule.h"
#include "MidiSerialListener.h"
#include "Sh101StyleSynth.h"

// --- Synth-Specific Module Headers ---
// #include "freqModSineModule.h"
// Our all-in-one synth voice

//==============================================================================
// Core 1: The Audio Thread
//==============================================================================
void main_core1() {
  // Define the audio hardware we are using
  using ActiveAudioOutput = I2sAudioOutput;

  // 1. Create the processing engine
  static AudioEngine engine(ActiveAudioOutput::NUM_CHANNELS,
                            ActiveAudioOutput::BUFFER_SIZE);

  // 2. Create audio modules in processing order
  static Sh101StyleSynth synth_voice((float)ActiveAudioOutput::SAMPLE_RATE);
  static GainModule master_gain((float)ActiveAudioOutput::SAMPLE_RATE);

  // 3. Add modules to engine in processing order (filter now per-voice)
  engine.addModule(&synth_voice);
  engine.addModule(&master_gain);

  // 4. Instantiate the audio hardware driver and give it the engine
  static ActiveAudioOutput audioOutput(engine);

  // 5. This call blocks forever and runs the audio loop
  audioOutput.start();
}

//==============================================================================
// Core 0: The Control Thread
//==============================================================================
int main() {
  // Initialize USB FIRST with default clock
  stdio_init_all();
  sleep_ms(1000); // Allow USB to enumerate
  
  // --- Overclocking Section (AFTER USB init) ---
  // Set the core voltage. VREG_VOLTAGE_1_15 is a safe level for a 250MHz overclock.
  vreg_set_voltage(VREG_VOLTAGE_1_15);
  sleep_ms(2); // Allow voltage to stabilize
  // Set the system clock to 250 MHz (the SDK takes the frequency in KHz)
  set_sys_clock_khz(250000, true);
  sleep_ms(2);
  // ----------------------------


  // IMPORTANT: Initialize the global parameter store BEFORE launching Core 1
  initialize_parameters();


  printf("LOG:--- Pico Synth (Integrated Voice) Initialized ---\n");
  printf("LOG: System clock is running at %lu kHz\n",
         clock_get_hz(clk_sys) / 1000);
  // Launch the audio engine on the second core
  multicore_launch_core1(main_core1);

  // Create the listeners that will run on this core
  MidiSerialListener midi_listener;

  // The main control loop for Core 0
  while (true) {
    midi_listener.update();
  }

  return 0; // Will never be reached
}