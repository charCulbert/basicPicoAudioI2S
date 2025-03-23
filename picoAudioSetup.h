#pragma once
#include "pico/audio_i2s.h"

#define SAMPLES_PER_BUFFER 512

// Initialize I2S for 16-bit stereo at 44.1 kHz and return the audio buffer pool.
inline struct audio_buffer_pool* init_audio() {
    static audio_format_t audio_format = {
        .sample_freq   = 22050,
        .format        = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = 2
    };

    static audio_buffer_format producer_format = {
        .format        = &audio_format,
        .sample_stride = 4  // 2 channels x 2 bytes
    };

    struct audio_buffer_pool* pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);

    audio_i2s_config_t config = {
        .data_pin       = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel    = 0,
        .pio_sm         = 0
    };

    const audio_format_t* out_format = audio_i2s_setup(&audio_format, &config);
    if (!out_format)
        panic("Failed to open I2S audio.\n");

    bool ok = audio_i2s_connect(pool);
    assert(ok);
    audio_i2s_set_enabled(true);
    return pool;
}

// The low-level loop: repeatedly takes a buffer, calls process_audio_block,
// and returns the buffer. This function is inline so it can be included in multiple translation units.
inline void run_audio_loop(struct audio_buffer_pool* pool, void (*process_audio_block)(int16_t*, uint32_t)) {
    while (true) {
        struct audio_buffer* buffer = take_audio_buffer(pool, true);
        if (!buffer)
            continue;
        int16_t* samples = reinterpret_cast<int16_t*>(buffer->buffer->bytes);
        uint32_t frames = buffer->max_sample_count;
        process_audio_block(samples, frames);
        buffer->sample_count = frames;
        give_audio_buffer(pool, buffer);
    }
}
