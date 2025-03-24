#include <cstdint>
#include "pico/stdlib.h"
#include "picoAudioSetup.h"
#include "AudioEngine.h"
#include "AudioModule.h"
#include "chocSineModule.h"
#include "freqModSineModule.h"


int main() {
    stdio_init_all();

    // Create our AudioEngine for 2 channels and SAMPLES_PER_BUFFER frames.
    const int channels = 2;
    const int frames = 512;  // Must match SAMPLES_PER_BUFFER in picoAudioSetup.h
    AudioEngine engine(channels, frames);

    // Add modules to the callback
    // ChocSineModule sineModule(128.0, 192.0, 22050.0, 0.1); // 440 Hz, 44.1 kHz sample rate, 50% volume
    FreqModSineModule fmModule(128.0, 20, 25, 22050, 0.1);
    engine.addModule(&fmModule);

    engine.start();

    return 0;
}
