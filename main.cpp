#include <cstdint>
#include <sys/unistd.h>
#include <cstdio>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "AudioEngine.h"
#include "ControlDefinitions.h" // <-- Include our new single source of truth
#include "picoAudoSetup_pwm.h"
#include "freqModSineModule.h"
#include "VCAEnvelopeModule.h"

//==============================================================================
// The ControlModule is now incredibly simple.
// It just executes the functions from the master list.
//==============================================================================
class ControlModule : public AudioModule {
public:
    ControlModule(FreqModSineModule* osc, VCAEnvelopeModule* env)
      : oscillator(osc), envelope(env) {}

    void process(choc::buffer::InterleavedView<float> /*buffer*/) override {
        while (multicore_fifo_rvalid()) {
            uint32_t cmd = multicore_fifo_pop_blocking();

            // Decode the command ID to find the action in our master list.
            size_t definition_index = cmd / 2; // Each definition has two commands (down/up)
            bool is_up_command = (cmd % 2 != 0);

            if (definition_index < g_controlDefinitions.size()) {
                const auto& def = g_controlDefinitions[definition_index];
                if (is_up_command) {
                    def.action_up(oscillator, envelope);
                } else {
                    def.action_down(oscillator, envelope);
                }
            }
        }
    }
private:
    FreqModSineModule* oscillator;
    VCAEnvelopeModule* envelope;
};

//==============================================================================
// Core 1 (No changes needed)
//==============================================================================
void main_core1() {
    printf("Audio core (core 1) running with fully automated controls.\n");
    AudioEngine engine(PwmAudioOutput::NUM_CHANNELS, PwmAudioOutput::BUFFER_SIZE);
    FreqModSineModule oscillator(128.0, 2.5, 5.0, PwmAudioOutput::SAMPLE_RATE, 1.0);
    VCAEnvelopeModule envelope(PwmAudioOutput::SAMPLE_RATE);
    envelope.setAttackTime(0.05); envelope.setDecayTime(0.3);
    envelope.setSustainLevel(0.7); envelope.setReleaseTime(1.2);
    ControlModule control(&oscillator, &envelope);
    engine.addModule(&control); engine.addModule(&oscillator); engine.addModule(&envelope);
    printf("Signal Path: Control -> Oscillator -> VCA Envelope -> Hardware\n");
    envelope.noteOn();
    PwmAudioOutput audioOutput(engine);
    printf("Starting PWM audio output...\n");
    audioOutput.start();
}

//==============================================================================
// Core 0: The main control core.
//==============================================================================
int main() {
    stdio_init_all();
    sleep_ms(2500);

    printf("--- Pico Modular Synth --- \n");
    printf("Controls (Down/Up):\n");
    // Generate help text automatically from the master list.
    for (const auto& def : g_controlDefinitions) {
        printf("  %c/%c  -> %s\n", def.key_down, def.key_up, def.label);
    }

    multicore_launch_core1(main_core1);

    while (true) {
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            // Find which command to send by looping through the master list.
            for (size_t i = 0; i < g_controlDefinitions.size(); ++i) {
                const auto& def = g_controlDefinitions[i];
                uint32_t command_to_send = 0;

                if (c == def.key_down) {
                    command_to_send = i * 2; // Down commands are even numbers (0, 2, 4...)
                    printf("core0: -> %s Down\n", def.label); // Print the command!
                } else if (c == def.key_up) {
                    command_to_send = i * 2 + 1; // Up commands are odd numbers (1, 3, 5...)
                    printf("core0: -> %s Up\n", def.label); // Print the command!
                }

                if (command_to_send != 0 || c == def.key_down) { // Handle index 0 case
                    multicore_fifo_push_blocking(command_to_send);
                    break; // Found it, stop searching
                }
            }
        }
        tight_loop_contents();
    }
    return 0;
}