#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include "pico/stdlib.h"
#include "pico/multicore.h"

// --- Include our new listener and the audio engine files ---
#include "MidiSerialListener.h"
#include "AudioEngine.h"
#include "PwmAudioOutput.h"
#include "freqModSineModule.h"
#include "VCAEnvelopeModule.h"
#include "I2sAudioOutput.h"    // <-- STEP 1: Add the new I2S header


// Helper to convert MIDI note number to frequency (still needed by Core 1)
static inline float midi_note_to_freq(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

//==============================================================================
// Core 1 Audio Processing remains here as it's launched by main()
//==============================================================================
class MidiControlModule : public AudioModule {
public:
    MidiControlModule(FreqModSineModule* osc, VCAEnvelopeModule* env)
      : oscillator(osc), envelope(env) {}

    void process(choc::buffer::InterleavedView<float> /*buffer*/) override {
        // This logic is unchanged.
        while (multicore_fifo_rvalid()) {
            uint32_t packet = multicore_fifo_pop_blocking();
            uint8_t command = (packet >> 24) & 0xFF;
            uint8_t data1   = (packet >> 16) & 0xFF;
            uint8_t data2   = (packet >> 8)  & 0xFF;

            switch (command) {
                case 0x90: { // NOTE_ON
                    lastVelocityVolume = data2 / 127.0f;
                    oscillator->setBaseFrequency(midi_note_to_freq(data1));
                    oscillator->setVolume(masterVolume * lastVelocityVolume);
                    envelope->noteOn();
                    break;
                }
                case 0x80: { // NOTE_OFF
                    envelope->noteOff();
                    break;
                }
                case 0xB0: { // CONTROL_CHANGE
                    float value_norm = data2 / 127.0f;
                    switch (data1) {
                        case 1:  oscillator->setModulationIndex(value_norm * 10.0f); break;
                        case 10: oscillator->setHarmonicityRatio(0.5f + (value_norm * 3.5f)); break;
                        case 74: envelope->setAttackTime(0.001f + (value_norm * 2.0f)); break;
                        case 71: envelope->setDecayTime(0.001f + (value_norm * 3.0f)); break;
                        case 72: envelope->setReleaseTime(0.01f + (value_norm * 5.0f)); break;
                        case 73: envelope->setSustainLevel(value_norm); break;
                        case 75:
                            masterVolume = value_norm;
                            oscillator->setVolume(masterVolume * lastVelocityVolume);
                            break;
                    }
                    break;
                }
            }
        }
    }
private:
    FreqModSineModule* oscillator;
    VCAEnvelopeModule* envelope;
    float masterVolume = 1.0f;
    float lastVelocityVolume = 1.0f;
};

//==============================================================================
// Core 1 Audio Processing - MODIFIED FOR I2S
//==============================================================================
// In main.cpp

//==============================================================================
// Core 1 Audio Processing - MODIFIED FOR I2S
//==============================================================================
void main_core1() {
    // ==================================================================
    // === CHOOSE YOUR AUDIO HARDWARE HERE (ONE-LINE CHANGE) ===
    //
    using ActiveAudioOutput = I2sAudioOutput;
    // using ActiveAudioOutput = PwmAudioOutput;
    //
    // ==================================================================


    // 1. Initialize the engine with the constants from our chosen hardware class.
    //    The rest of the code is now generic and doesn't care if it's I2S or PWM.
    AudioEngine engine(ActiveAudioOutput::NUM_CHANNELS, ActiveAudioOutput::BUFFER_SIZE);

    // 2. Create your synth modules, using the chosen sample rate.
    FreqModSineModule oscillator(440.0, 1.0, 0.0, ActiveAudioOutput::SAMPLE_RATE, 1.0);
    VCAEnvelopeModule envelope(ActiveAudioOutput::SAMPLE_RATE);
    MidiControlModule midiControl(&oscillator, &envelope);

    // 3. Add modules to the engine (same as before).
    engine.addModule(&midiControl);
    engine.addModule(&oscillator);
    engine.addModule(&envelope);

    // 4. Instantiate the CHOSEN audio driver, passing it the engine.
    ActiveAudioOutput audioOutput(engine);

    // 5. Start the blocking audio loop. This will never return.
    audioOutput.start();
}

//==============================================================================
// The main() function on Core 0 remains completely unchanged!
//==============================================================================
int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("\n--- Pico Modular Synth (I2S Output) ---\n");

    // Launch the audio engine on the second core.
    multicore_launch_core1(main_core1);

    // Create an instance of our listener class.
    MidiSerialListener listener;

    // Start the infinite listening loop on Core 0.
    listener.run();

    return 0;
}