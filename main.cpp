#include <cstdint>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"
// --- Core Application Headers ---
#include "ParameterStore.h"
#include "RotaryEncoderListener.h"
#include "AudioEngine.h"
#include "I2sAudioOutput.h"
// #include "PolyFreqModSineModule.h"

// --- Synth-Specific Module Headers ---
#include "freqModSineModule.h" // Our all-in-one synth voice


//==============================================================================
// Core 1: The Audio Thread
//==============================================================================
void main_core1() {
    // Define the audio hardware we are using
    using ActiveAudioOutput = I2sAudioOutput;

    // 1. Create the processing engine
    static AudioEngine engine(ActiveAudioOutput::NUM_CHANNELS, ActiveAudioOutput::BUFFER_SIZE);

    // 2. Create the "smart" synth voice module. It handles everything.
    PolyphonicFMModule synth_voice(ActiveAudioOutput::SAMPLE_RATE);

    // 3. Add the single synth voice to the engine.
    engine.addModule(&synth_voice);

    // 4. Instantiate the audio hardware driver and give it the engine
    static ActiveAudioOutput audioOutput(engine);

    // 5. This call blocks forever and runs the audio loop
    audioOutput.start();
}

//==============================================================================
// Core 0: The Control Thread
//==============================================================================
int main() {
    // --- Overclocking Section ---
    // Set the core voltage. VREG_VOLTAGE_1_15 is a safe level for a 250MHz overclock.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2); // Allow voltage to stabilize
    // Set the system clock to 250 MHz (the SDK takes the frequency in KHz)
    set_sys_clock_khz(250000, true);
    sleep_ms(2);
    // ----------------------------


    stdio_init_all();

    // IMPORTANT: Initialize the global parameter store BEFORE launching Core 1
    initialize_parameters();

    sleep_ms(2000);#include "MidiSerialListener.h"

    printf("LOG:--- Pico Synth (Integrated Voice) Initialized ---\n");
    printf("LOG: System clock is running at %lu kHz\n", clock_get_hz(clk_sys) / 1000);
    // Launch the audio engine on the second core
    multicore_launch_core1(main_core1);

    // Create the listeners that will run on this core
    MidiSerialListener midi_listener;
    RotaryEncoderListener rotary_listener;

    // The main control loop for Core 0
    while (true) {
        midi_listener.update();
        rotary_listener.update();
    }

    return 0; // Will never be reached
}