#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

/**
 * Initializes 16-bit stereo I2S at 44.1 kHz, sets up a ring of buffers,
 * and enables audio output. Returns a pointer to the buffer pool used by
 * the ring-buffer system.
 *
 * Interfacing with the pico-sdk C SDK and i2s audio from pico-extras
 *
 * @return audio_buffer_pool* pointer used for audio data buffers
 */
inline struct audio_buffer_pool* init_audio()
{
    // NOTE: match the struct's actual field order. In many SDK versions, sample_freq is first.
    static audio_format_t audio_format = {
        .sample_freq   = 44100,                       // First field
        .format        = AUDIO_BUFFER_FORMAT_PCM_S16, // Second field
        .channel_count = 2                            // Third field
    };

    // For stereo 16-bit, each frame is 2 channels * 2 bytes = 4 bytes
    static struct audio_buffer_format producer_format = {
        .format        = &audio_format,
        .sample_stride = 4 // bytes per stereo frame
    };

    // Number of frames per buffer
    const uint32_t SAMPLES_PER_BUFFER = 256;

    // Create a pool of 3 buffers, each with SAMPLES_PER_BUFFER frames
    struct audio_buffer_pool* producer_pool =
        audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);

    // The I2S config structure: pins and DMA/PIO details.
    struct audio_i2s_config config = {
        .data_pin       = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel    = 0,
        .pio_sm         = 0
    };

    // Set up the I2S output at 44.1 kHz, 16-bit stereo
    const audio_format_t* out_format = audio_i2s_setup(&audio_format, &config);
    if (!out_format) {
        panic("Failed to open I2S audio.\n");
    }

    // Connect our buffer pool to the I2S system
    bool ok = audio_i2s_connect(producer_pool);
    assert(ok);

    // Enable audio output
    audio_i2s_set_enabled(true);

    return producer_pool;
}
