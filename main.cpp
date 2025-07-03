#include <cstdint>
#include <sys/unistd.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "AudioEngine.h"
#include "AudioModule.h"
#include "freqModSineModule.h"
#include "cstdio"
#include "picoAudoSetup_pwm.h"


void main_core1() {
    printf("Audio core (core 1) is running with decoupled architecture.\n");

    // 1. Create the processing engine. It knows nothing about hardware.
    AudioEngine engine(PwmAudioOutput::NUM_CHANNELS, PwmAudioOutput::BUFFER_SIZE);

    // 2. Create your audio modules.
    FreqModSineModule fmModule(
        128.0,                          // baseFrequency
        9.5,                            // harmonicityRatio
        3.6,                            // modulationIndex
        PwmAudioOutput::SAMPLE_RATE,    // sampleRate (from the output driver's config)
        0.1                             // volume
    );

    // 3. Add modules to the engine.
    engine.addModule(&fmModule);

    // 4. Create the hardware output driver, giving it the engine to play.
    PwmAudioOutput audioOutput(engine);

    // 5. Start the hardware driver. This will begin the real-time loop.
    printf("Starting PWM audio output...\n");
    audioOutput.start();
}

int main() {
    stdio_init_all();
    sleep_ms(2500);
    printf("Hello from core 0");
    multicore_launch_core1(main_core1);


    // Main loop for serial input.
    // For this example, we use simple key presses:
    //   'a' sends volume down, 's' sends volume up,
    //   'd' sends modulation down, 'f' sends modulation up,
    //   'g' sends harmonicity down, 'h' sends harmonicity up.
    while (true) {
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            // Adjust volume.
            if (c == 'a') {
                printf("core0: volDown\n");
                multicore_fifo_push_blocking(CMD_VOL_DOWN);
            }
            if (c == 's') {
                printf("core0: volUp\n");
                multicore_fifo_push_blocking(CMD_VOL_UP);
            }
            // Adjust modulation index.
            if (c == 'd') {
                printf("core0: modDown\n");
                multicore_fifo_push_blocking(CMD_MOD_DOWN);
            }
            if (c == 'f') {
                printf("core0: modUp\n");
                multicore_fifo_push_blocking(CMD_MOD_UP);
            }
            // Adjust harmonicity ratio.
            if (c == 'g') {
                printf("core0: harmDown\n");
                multicore_fifo_push_blocking(CMD_HARM_DOWN);
            }
            if (c == 'h') {
                printf("core0: harmUp\n");
                multicore_fifo_push_blocking(CMD_HARM_UP);
            }
            else
            {}
        }
        tight_loop_contents();
    }
    return 0;
}


