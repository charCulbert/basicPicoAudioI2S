/**
 * AudioEngine.h - Central Audio Processing Engine
 * 
 * The AudioEngine manages a collection of AudioModule instances and coordinates
 * their processing. It is hardware-agnostic and focuses purely on audio processing.
 * All audio uses 16.15 fixed-point arithmetic for optimal RP2040 performance.
 */

#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"
#include "Fix15.h"

/**
 * AudioEngine - Central coordinator for audio processing modules
 * 
 * Design Philosophy:
 * - Hardware-agnostic: doesn't know about I2S, PWM, or other hardware details
 * - Module-based: audio processing is done by composable AudioModule instances
 * - Fixed-point: all processing uses fix15 arithmetic for RP2040 optimization
 * - Real-time safe: no dynamic memory allocation during processing
 * 
 * Processing Flow:
 * 1. Hardware driver calls processNextBlock() with an empty buffer
 * 2. Engine clears the buffer to ensure clean slate
 * 3. Each registered module processes and mixes into the buffer
 * 4. Final mixed output is returned to hardware driver
 */
class AudioEngine {
public:
    AudioEngine(int channels, int frames)
      : numChannels(channels), numFrames(frames)
    {
        // This engine doesn't allocate its own primary buffer anymore.
        // It's designed to fill a buffer provided by the caller (the hardware driver).
    }

    void addModule(AudioModule* module) {
        modules.push_back(module);
    }

    /**
     * @brief The core processing function.
     * It clears the provided buffer, then iterates through all registered modules,
     * allowing each to add its output to the buffer.
     * @param bufferToFill An interleaved CHOC view representing the memory to write audio into.
     */
    void processNextBlock(choc::buffer::InterleavedView<fix15>& bufferToFill) {

        // 1. Clear the buffer to ensure a clean slate for mixing.
        bufferToFill.clear();

        // 2. Process all modules, mixing their output into the buffer.
        for (auto module : modules) {
            module->process(bufferToFill);
        }

    }

private:
    int numChannels, numFrames;
    std::vector<AudioModule*> modules;
};