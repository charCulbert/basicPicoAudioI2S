// AudioEngine.h

#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h" // Abstract base class for modules



/**
 * AudioEngine is a pure processing class, completely decoupled from hardware.
 * Its sole responsibility is to manage a list of AudioModules and, when requested,
 * process them to fill a provided floating-point audio buffer.
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
    void processNextBlock(choc::buffer::InterleavedView<float>& bufferToFill) {


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