#include <cstdint>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "picoAudioSetup.h"
#include "AudioEngine.h"
#include "AudioModule.h"
#include "chocSineModule.h"
#include "freqModSineModule.h"
#include "cstdio"


void main_core1() {
    printf("Hello from core 1!\n");
    // Create our AudioEngine for 2 channels and SAMPLES_PER_BUFFER frames.
    const int channels = 2;
    const int frames = 128;  // Must match SAMPLES_PER_BUFFER in picoAudioSetup.h
    AudioEngine engine(channels, frames);

    // Add modules to the callback
//     ChocSineModule sineModule(64.0, 64.0, 22050.0, 0.1); // 440 Hz, 44.1 kHz sample rate, 50% volume
// engine.addModule(&sineModule);
    FreqModSineModule fmModule(128.0, 1.5, 1, 22050, 0.1);
    engine.addModule(&fmModule);


    engine.start();

}

int main() {
    stdio_init_all();
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


