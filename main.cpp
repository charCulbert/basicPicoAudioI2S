    #include <cstdint>
    #include "pico/stdlib.h"
    #include "pico/multicore.h"

    // --- Core Application Headers ---
    #include "ParameterStore.h"
    #include "MidiSerialListener.h"
    #include "RotaryEncoderListener.h"
    #include "AudioEngine.h"
    #include "I2sAudioOutput.h"
    #include "Oscillators.h"
    // --- Synth-Specific Module Headers ---
#include <sys/stat.h>

    #include "freqModSineModule.h" // Our all-in-one synth voice


    //==============================================================================
    // Core 1: The Audio Thread
    //==============================================================================
    void main_core1() {
        // Define the audio hardware we are using
        using ActiveAudioOutput = I2sAudioOutput;
        constexpr float SAMPLE_RATE = ActiveAudioOutput::SAMPLE_RATE;
        constexpr float BUFFER_SIZE = ActiveAudioOutput::BUFFER_SIZE;

        // 1. Create the processing engine
        static AudioEngine engine(ActiveAudioOutput::NUM_CHANNELS, BUFFER_SIZE);

        // SlowMathOscillator myOsc;
        // FastLutOscillator myOsc;
        // SoftwareInterpOscillator myOsc;
        // HardwareAddressOscillator myOsc;
        // HardwareInterpOscillator myOsc;

        SoftwareInterpOscillator myOsc;
        myOsc.setFrequency(60.0f, SAMPLE_RATE);
        myOsc.gain = 0.005f; // Set the gain here!
        engine.addModule(&myOsc);

        SoftwareInterpOscillator myOsc2;
        myOsc2.setFrequency(200.0f, SAMPLE_RATE);
        myOsc2.gain = 0.004f; // Set the gain here!
        engine.addModule(&myOsc2);

        SoftwareInterpOscillator myOsc3;
        myOsc3.setFrequency(800.0f, SAMPLE_RATE);
        myOsc3.gain = 0.003f; // Set the gain here!
        engine.addModule(&myOsc3);


        SoftwareInterpOscillator myOsc4;
        myOsc4.setFrequency(1900.0f, SAMPLE_RATE);
        myOsc4.gain = 0.002f; // Set the gain here!
        engine.addModule(&myOsc4);

        SoftwareInterpOscillator myOsc5;
        myOsc5.setFrequency(8000.0f, SAMPLE_RATE);
        myOsc5.gain = 0.001f; // Set the gain here!
        engine.addModule(&myOsc5);



        // 4. Instantiate the audio hardware driver and give it the engine
       static ActiveAudioOutput audioOutput(engine);

        // 5. This call blocks forever and runs the audio loop
        audioOutput.start();
    }

    //==============================================================================
    // Core 0: The Control Thread
    //==============================================================================
    int main() {
        stdio_init_all();

        // IMPORTANT: Initialize the global parameter store BEFORE launching Core 1
        initialize_parameters();

        sleep_ms(2000);
        printf("LOG:--- Pico Synth (Integrated Voice) Initialized ---\n");

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