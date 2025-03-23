//
// Created by char on 3/23/25.
//

#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <vector>
#include "choc/audio/choc_SampleBuffers.h"  // CHOC buffers header
#include <cstdint>
#include "AudioModule.h"
#include "AudioEngine.h"


//==============================================================================
// AudioEngine wraps the audio callback and CHOC buffer processing.
// It creates a float scratch buffer, wraps it as a CHOC InterleavedView,
// calls all registered modules, and converts the float mix to int16.
class AudioEngine {
public:
    AudioEngine(int channels, int frames)
      : numChannels(channels), numFrames(frames)
    {
        // Allocate a float buffer for mixing.
        floatBuffer.resize(numChannels * frames, 0.0f);
        // Create an interleaved CHOC view over the float buffer.
        floatView = choc::buffer::createInterleavedView<float>(floatBuffer.data(), numChannels, frames);
    }

    // Add an audio module to be processed each block.
    void addModule(AudioModule* module) {
        modules.push_back(module);
    }

    // Process the next audio block.
    // 1. Clears the float buffer.
    // 2. Calls process() on each module.
    // 3. Converts the float buffer (assumed to be in range [-1, 1]) to int16 samples.
    void processNextBlock(int16_t* output) {
        // Clear the float buffer to silence.
        choc::buffer::setAllFrames(floatView, []() { return 0.0f; });
        // Render each module into the float buffer.
        for (auto module : modules)
            module->process(floatView);
        // Convert float values to int16.
        for (uint32_t i = 0; i < floatBuffer.size(); ++i) {
            float s = floatBuffer[i];
            // Clamp to [-1, 1].
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            output[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

private:
    int numChannels, numFrames;
    std::vector<float> floatBuffer;
    choc::buffer::InterleavedView<float> floatView;
    std::vector<AudioModule*> modules;
};



#endif //AUDIOENGINE_H
