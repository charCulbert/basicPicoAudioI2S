// PwmAudioOutput.h

#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

// Pico SDK & Hardware Includes
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// CHOC & Module Includes
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioEngine.h" // Include the pure processing engine

/**
 * PwmAudioOutput is the hardware driver for audio output.
 * It encapsulates all PWM, DMA, and IRQ setup. It runs the real-time audio loop
 * and requests new audio data from an AudioEngine instance when needed.
 */
class PwmAudioOutput {
public:
    // --- Core Audio Configuration ---
    static constexpr int AUDIO_PIN = 2;
    static constexpr int SAMPLE_RATE = 22050;
    static constexpr int PWM_WRAP = 254;
    static constexpr int BUFFER_SIZE = 128;
    static constexpr int NUM_CHANNELS = 2; // The number of channels the AudioEngine processes

    /**
     * @brief Constructor that takes a reference to an AudioEngine.
     * It initializes all hardware and sets up the connection to the engine.
     * @param engine The AudioEngine instance that will generate the audio samples.
     */
    PwmAudioOutput(AudioEngine& engine) : audioEngine(engine) {
        // --- 1. Hardware Setup (PWM & DMA) ---
        gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
        int pwm_slice_num = pwm_gpio_to_slice_num(AUDIO_PIN);
        float clock_div = (float)clock_get_hz(clk_sys) / (SAMPLE_RATE * (PWM_WRAP + 1));
        pwm_config config = pwm_get_default_config();
        pwm_config_set_wrap(&config, PWM_WRAP);
        pwm_config_set_clkdiv(&config, clock_div);
        pwm_init(pwm_slice_num, &config, true);

        dma_chan = dma_claim_unused_channel(true);
        dma_channel_config dma_config = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_write_increment(&dma_config, false);
        channel_config_set_dreq(&dma_config, DREQ_PWM_WRAP0 + pwm_slice_num);
        dma_channel_configure(dma_chan, &dma_config, &pwm_hw->slice[pwm_slice_num].cc, NULL, 0, false);

        // --- 2. IRQ Setup ---
        instance = this;
        dma_channel_set_irq0_enabled(dma_chan, true);
        irq_set_exclusive_handler(DMA_IRQ_0, static_dma_irh);
        irq_set_enabled(DMA_IRQ_0, true);
    }

    /**
     * @brief Starts the blocking, real-time audio loop. This will not return.
     */
    void start() {
        fillAndConvertNextBuffer();
        dma_buffer_to_fill_idx = 1;
        fillAndConvertNextBuffer();
        dma_buffer_to_fill_idx = 0;

        dma_channel_set_read_addr(dma_chan, audio_buffers[0], false);
        dma_channel_set_trans_count(dma_chan, BUFFER_SIZE, true);

        while (true) {
            int last_processed_idx = dma_buffer_to_fill_idx;
            while (last_processed_idx == dma_buffer_to_fill_idx) {
                tight_loop_contents();
            }
            fillAndConvertNextBuffer();
        }
    }

private:
    /**
     * This is the bridge function. It asks the AudioEngine to fill a float buffer,
     * then it converts that float data into the uint16_t format required by the PWM hardware.
     */
    void fillAndConvertNextBuffer() {
        // 1. Create a CHOC view pointing to our temporary float buffer.
        auto float_workspace_view = choc::buffer::createInterleavedView<float>(
            dsp_float_buffer, NUM_CHANNELS, BUFFER_SIZE
        );

        // 2. Ask the AudioEngine to process its modules and fill our workspace.
        audioEngine.processNextBlock(float_workspace_view);

        // 3. Convert the float buffer to the uint16_t hardware buffer.
        // This is where the format conversion (mixing, clipping, scaling) happens.
        uint16_t* hardware_buffer = audio_buffers[dma_buffer_to_fill_idx];
        for (int i = 0; i < BUFFER_SIZE; ++i) {
            // Mix stereo to mono (average the channels)
            float mono_sample = (dsp_float_buffer[i * 2] + dsp_float_buffer[i * 2 + 1]) * 0.5f;

            // Clip and scale to PWM range
            mono_sample = std::max(-1.0f, std::min(1.0f, mono_sample));
            hardware_buffer[i] = static_cast<uint16_t>(((mono_sample + 1.0f) / 2.0f) * PWM_WRAP);
        }
    }

    void dma_irh() {
        dma_hw->ints0 = (1u << dma_chan);
        dma_channel_set_read_addr(dma_chan, audio_buffers[dma_buffer_to_fill_idx], true);
        dma_buffer_to_fill_idx = 1 - dma_buffer_to_fill_idx;
    }

    // --- Member Variables ---
    AudioEngine& audioEngine; // Reference to the object that generates audio
    int dma_chan;
    uint16_t audio_buffers[2][BUFFER_SIZE];
    // Temporary buffer for the AudioEngine to fill before we convert it
    float dsp_float_buffer[BUFFER_SIZE * NUM_CHANNELS];
    volatile int dma_buffer_to_fill_idx = 0;

    // --- Singleton for IRQ ---
    inline static PwmAudioOutput* instance = nullptr;
    static void static_dma_irh() {
        if (instance) instance->dma_irh();
    }
};