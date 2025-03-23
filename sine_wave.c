/**
 * main.c
 *
 * Minimal I2S demo in C. This calls out to C++ code for actual audio generation.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

// This function is implemented in sine_osc.cpp. We use extern "C" so C can call it.
#ifdef __cplusplus
extern "C" {
#endif
void fill_audio_block(int16_t* samples, uint32_t numSamples);
#ifdef __cplusplus
}
#endif

// How many samples per audio buffer
#define SAMPLES_PER_BUFFER 256

//--------------------------------------------------------------------------------
// Set up the I2S audio system, returning a pointer to an audio_buffer_pool.
//--------------------------------------------------------------------------------
static struct audio_buffer_pool* init_audio(void)
{
    // We'll use 16-bit stereo at 44.1 kHz
    static audio_format_t audio_format = {
        .format        = AUDIO_BUFFER_FORMAT_PCM_S16, // 16-bit
        .sample_freq   = 44100,                       // 44.1 kHz
        .channel_count = 2,                           // stereo
    };

    // We produce 2 bytes per sample (16-bit), then there are 2 channels
    static struct audio_buffer_format producer_format = {
        .format        = &audio_format,
        .sample_stride = 4 //bytes per frame of interleaved audio
    };

    // Make a pool of 3 buffers, each with SAMPLES_PER_BUFFER
    struct audio_buffer_pool* producer_pool =
        audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);

    // I2S config: Pins are set via compile definitions in CMake
    struct audio_i2s_config config = {
        .data_pin       = PICO_AUDIO_I2S_DATA_PIN,      // e.g. 10
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,// e.g. 11 (and 12 for LRCLK)
        .dma_channel    = 0,
        .pio_sm         = 0
    };

    // Initialize the I2S system with this config
    const audio_format_t* out_format = audio_i2s_setup(&audio_format, &config);
    if (!out_format) {
        panic("Failed to open I2S audio.\n");
    }

    // Connect our buffer pool to the audio system
    bool ok = audio_i2s_connect(producer_pool);
    assert(ok);

    // Enable audio output
    audio_i2s_set_enabled(true);

    return producer_pool;
}

//--------------------------------------------------------------------------------
// main: sets up audio, then loops forever, filling audio buffers from the C++ function
//--------------------------------------------------------------------------------
int main(void)
{
    stdio_init_all(); // for USB serial console

    // 1) Initialize I2S
    struct audio_buffer_pool* ap = init_audio();

    // 2) Main loop(audio callback):
    while (true)
    {
        // 2A) Take an empty buffer from the pool
        struct audio_buffer* buffer = take_audio_buffer(ap, true);

        // 2B) Fill it by calling our external C++ function
        int16_t* samples = (int16_t*) buffer->buffer->bytes;
        uint32_t numFrames = buffer->max_sample_count;

        // Fill it with our stereo sine wave
        fill_audio_block(samples, numFrames);

        // Hand it back for playback
        buffer->sample_count = numFrames;
        give_audio_buffer(ap, buffer);
    }
    return 0;
}
