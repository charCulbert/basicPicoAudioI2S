#include <cstdint>
#include "pico/stdlib.h"
#include "picoAudioSetup.h"
#include "AudioEngine.h"
#include "AudioModule.h"
#include "chocSineModule.h"


// Global pointer to our AudioEngine instance.
static AudioEngine* gEngine = nullptr;

/**
 * process_audio_block is called by the I2S ring-buffer loop.
 * It delegates to our AudioEngine to process the next block.
 */
void process_audio_block(int16_t* samples, uint32_t numFrames) {
    if (gEngine)
        gEngine->processNextBlock(samples);
    else {
        // If no engine is set, output silence.
        for (uint32_t i = 0; i < numFrames * 2; ++i)
            samples[i] = 0;
    }
}

int main() {
    stdio_init_all();

    // Initialize I2S audio.
    struct audio_buffer_pool* ap = init_audio();

    // Create our AudioEngine for 2 channels and SAMPLES_PER_BUFFER frames.
    const int channels = 2;
    const int frames = 256;  // Must match SAMPLES_PER_BUFFER in picoAudioSetup.h
    AudioEngine engine(channels, frames);
    gEngine = &engine;

    // Add modules to the callback
    ChocSineModule sineModule(440.0, 44100.0, 0.5); // 440 Hz, 44.1 kHz sample rate, 50% volume
    engine.addModule(&sineModule);


    // Main I2S ring-buffer loop.
    while (true) {
        struct audio_buffer* buffer = take_audio_buffer(ap, true);
        if (!buffer)
            continue;
        // The DMA buffer holds stereo frames (2 samples per frame).
        int16_t* samples = reinterpret_cast<int16_t*>(buffer->buffer->bytes);
        uint32_t numFrames = buffer->max_sample_count;
        // Process the next block using our AudioEngine.
        process_audio_block(samples, numFrames);
        buffer->sample_count = numFrames;
        give_audio_buffer(ap, buffer);
    }
    return 0;
}
