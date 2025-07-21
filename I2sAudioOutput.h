// I2sAudioOutput.h
#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

// Pico SDK & Hardware Includes
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// The header file generated from our .pio file
#include "audio_i2s.pio.h"

// CHOC & Module Includes
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioEngine.h"

// include Fix15 stuff
#include "Fix15.h"

/**
 * I2sAudioOutput - High-Quality I2S Audio Driver for RP2040
 * 
 * Professional-grade stereo I2S (Inter-IC Sound) audio output implementation using
 * the RP2040's PIO (Programmable I/O) system for precise timing and DMA for
 * low-latency, continuous audio streaming.
 * 
 * Architecture:
 * - PIO State Machine: Generates I2S protocol timing (LRCLK, BCLK, DATA)
 * - Double-Buffered DMA: Continuous audio transfer without CPU intervention
 * - Fixed-Point Audio: All processing uses 16.15 format for optimal performance
 * - Real-Time Processing: Audio engine called via DMA interrupt for low latency
 * 
 * Hardware Requirements:
 * - GPIO 19: LRCLK (Left/Right Clock - Word Select)
 * - GPIO 20: BCLK (Bit Clock - Serial Clock)  
 * - GPIO 21: SDATA (Serial Data Output)
 * - GPIO 26: Debug pin (timing analysis)
 * 
 * Compatible with standard I2S DACs, audio codecs, and digital audio interfaces.
 * Interface matches PwmAudioOutput for easy hardware abstraction swapping.
 */
class I2sAudioOutput {
public:
    // --- Core Audio Configuration ---
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int BUFFER_SIZE = 64; // Number of L/R sample pairs per buffer
    static constexpr int NUM_CHANNELS = 2;     // Stereo

    // --- Hardware Pin Cofiguration ---
    // By default, the PIO program expects:
    //   - CLOCK_PIN_BASE     = LRCLK
    //   - CLOCK_PIN_BASE + 1 = BCLK
    static constexpr int CLOCK_PIN_BASE = 19; // This will be the LRCLK pin
    static constexpr int DATA_PIN = 21;
    static constexpr int DEBUG_PIN = 26; // <<< ADD THIS: Define our debug pin


    /**
     * @brief Constructor that takes a reference to an AudioEngine.
     * It initializes all PIO, DMA, and IRQ hardware.
     * @param engine The AudioEngine instance that will generate the audio samples.
     */
    I2sAudioOutput(AudioEngine& engine) : audioEngine(engine) {
        // --- 1. Debug Pin Setup --- // <<< ADD THIS SECTION
        gpio_init(DEBUG_PIN);
        gpio_set_dir(DEBUG_PIN, GPIO_OUT);
        gpio_put(DEBUG_PIN, false); // Ensure it starts low

        // --- 1. PIO Setup ---
        pio = pio0; // Use pio0
        pio_sm = pio_claim_unused_sm(pio, true);
        uint offset = pio_add_program(pio, &audio_i2s_program);

        // Calculate the PIO clock divider.
        // The I2S protocol requires a specific clock frequency relationship.
        // For each stereo sample (32 bits total), we need to generate 32 BCLK cycles.
        // Our PIO program (audio_i2s.pio) takes 2 PIO clock cycles to output 1 bit of data
        // (one cycle for BCLK low, one for BCLK high).
        //
        // Therefore, the total number of PIO cycles per stereo sample is:
        //   32 bits/sample * 2 PIO cycles/bit = 64 PIO cycles/sample
        //
        // The required PIO frequency is:
        //   SAMPLE_RATE * 64
        //
        // And the clock divider is:
        //   System Clock / (SAMPLE_RATE * 64)
        float clock_div = (float)clock_get_hz(clk_sys) / (float)(SAMPLE_RATE * 64);

        // Get the default PIO config and modify it for our needs
        pio_sm_config sm_config = audio_i2s_program_get_default_config(offset);

        sm_config_set_out_pins(&sm_config, DATA_PIN, 1);
        sm_config_set_sideset_pins(&sm_config, CLOCK_PIN_BASE);
        sm_config_set_out_shift(&sm_config, false, true, 32); // Shift out, autopull, 32-bit threshold
        sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_TX); // Join FIFOs to make a single 8-word TX FIFO
        sm_config_set_clkdiv(&sm_config, clock_div);

        pio_sm_init(pio, pio_sm, offset, &sm_config);

        // --- This is the NEW, correct code ---
        pio_gpio_init(pio, DATA_PIN);
        pio_gpio_init(pio, CLOCK_PIN_BASE);
        pio_gpio_init(pio, CLOCK_PIN_BASE + 1);

        pio_sm_set_consecutive_pindirs(pio, pio_sm, DATA_PIN, 1, true);
        pio_sm_set_consecutive_pindirs(pio, pio_sm, CLOCK_PIN_BASE, 2, true);

        // --- 2. DMA Setup ---
        dma_chan = dma_claim_unused_channel(true);
        dma_channel_config dma_config = dma_channel_get_default_config(dma_chan);

        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);     // Transfer 32-bit words
        channel_config_set_read_increment(&dma_config, true);                // Increment read address
        channel_config_set_write_increment(&dma_config, false);              // Don't increment write address
        channel_config_set_dreq(&dma_config, pio_get_dreq(pio, pio_sm, true)); // Triggered by PIO TX FIFO

        dma_channel_configure(dma_chan, &dma_config, &pio->txf[pio_sm], NULL, 0, false);

        // --- 3. IRQ Setup ---
        instance = this;
        dma_channel_set_irq0_enabled(dma_chan, true);
        irq_set_exclusive_handler(DMA_IRQ_0, static_dma_irh);
        irq_set_enabled(DMA_IRQ_0, true);

        // --- 4. Start the PIO state machine ---
        pio_sm_set_enabled(pio, pio_sm, true);
    }

    /**
     * @brief Starts the blocking, real-time audio loop. This will not return.
     */
    void start() {
        // Pre-fill the buffer that the main loop will fill *second*.
        dma_buffer_to_fill_idx = 1;
        fillAndConvertNextBuffer();

        // Fill the buffer that the DMA will play *first*.
        dma_buffer_to_fill_idx = 0;
        fillAndConvertNextBuffer();

        // Start the first DMA transfer. We transfer buffer 1, so the main loop
        // can immediately start filling buffer 0.
        dma_channel_transfer_from_buffer_now(dma_chan, audio_buffers[1], BUFFER_SIZE);

        while (true) {
            // Wait for the IRQ to signal that the buffer we *just* filled has been
            // picked up, and the other buffer is now free for us to fill.
            int last_processed_idx = dma_buffer_to_fill_idx;
            while (last_processed_idx == dma_buffer_to_fill_idx) {
                tight_loop_contents(); // or __wfi() to save power and wait for interrupt
            }

            // The IRQ has flipped the index, so we can now fill the buffer
            // that just finished playing.
            fillAndConvertNextBuffer();
        }
    }

private:
    /**
     * This is the bridge function. It asks the AudioEngine to fill a float buffer,
     * then it converts that float data into the packed uint32_t format required by the PIO/DMA hardware.
     */
    void fillAndConvertNextBuffer() {
        gpio_put(DEBUG_PIN, true); // <<< ADD THIS: Set pin HIGH at the start

        // 1. Create a CHOC view pointing to our temporary float buffer.
        auto float_workspace_view = choc::buffer::createInterleavedView<fix15>(
            dsp_fix15_buffer, NUM_CHANNELS, BUFFER_SIZE
        );

        // 2. Ask the AudioEngine to process its modules and fill our workspace.
        audioEngine.processNextBlock(float_workspace_view); //make audioengine handle fix15

        // 3. Convert the float buffer to the uint32_t hardware buffer.
        uint32_t* hardware_buffer = audio_buffers[dma_buffer_to_fill_idx];

        for (int i = 0; i < BUFFER_SIZE; ++i) {
            // take the fix15 samples using just the 16 leftmost/integer bits... will this work as is?
            int16_t sample_l_s16 = dsp_fix15_buffer[i * 2 + 0];
            int16_t sample_r_s16 = dsp_fix15_buffer[i * 2 + 1];

            // Pack the two 16-bit samples into a single 32-bit word.
            // Per the PIO program comment: | 31:16 sample ws=0 | 15:0 sample ws=1 |
            // Let's assume Right Channel is ws=0 and Left Channel is ws=1.
            hardware_buffer[i] = (uint32_t)((uint16_t)sample_r_s16) << 16 | (uint16_t)sample_l_s16;
        }

        gpio_put(DEBUG_PIN, false); // <<< ADD THIS: Set pin LOW at the end
    }

    /**
     * @brief DMA Interrupt Handler. This is called when a buffer transfer completes.
     * It immediately chains the next buffer to the DMA to ensure continuous audio.
     */
    void dma_irh() {

        // Clear the interrupt request flag
        dma_hw->ints0 = (1u << dma_chan);

        // Give the next buffer (the one the main loop just filled) to the DMA
        dma_channel_set_read_addr(dma_chan, audio_buffers[dma_buffer_to_fill_idx], true);

        // Flip the index to signal to the main loop that it can now fill the other buffer.
        dma_buffer_to_fill_idx = 1 - dma_buffer_to_fill_idx;

    }

    // --- Member Variables ---
    AudioEngine& audioEngine;
    PIO pio;
    uint pio_sm;
    uint dma_chan;

    // Double-buffered audio data for the DMA
    uint32_t audio_buffers[2][BUFFER_SIZE];

    // Temporary workspace for the AudioEngine to fill with float samples
    fix15 dsp_fix15_buffer[BUFFER_SIZE * NUM_CHANNELS];

    // Index of the buffer for the main loop to fill next (0 or 1).
    // `volatile` is important as it's modified by an IRQ.
    volatile int dma_buffer_to_fill_idx = 0;

    // --- Singleton for IRQ ---


    // This allows the static IRQ handler to call our non-static member function.
    inline static I2sAudioOutput* instance = nullptr;
    static void static_dma_irh() {
        if (instance) instance->dma_irh();
    }
};