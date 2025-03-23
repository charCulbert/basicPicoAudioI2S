#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include "picoAudioSetup.h"
#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"  // Abstract base class for modules

/**
 * AudioEngine wraps the I2S processing loop and CHOC buffer mixing.
 * It creates the I2S pool (via init_audio()) and a scratch float buffer,
 * then in start() it calls the inline run_audio_loop() function.
 */
class AudioEngine {
public:
    AudioEngine(int channels, int frames)
      : numChannels(channels), numFrames(frames)
    {
        // Create the I2S pool.
        audioPool = init_audio();
        // Allocate scratch buffer and wrap it as a CHOC interleaved view.
        floatBuffer.resize(numChannels * frames, 0.0f);
        floatView = choc::buffer::createInterleavedView<float>(floatBuffer.data(), numChannels, frames);
    }

    void addModule(AudioModule* module) {
        modules.push_back(module);
    }

    // The static callback to be passed to run_audio_loop.
    static void audioCallback(int16_t* samples, uint32_t frames) {
        instance->processNextBlock(samples, frames);
    }

    // start() simply calls run_audio_loop() from picoAudioSetup.h.
    void start() {
        // Set the singleton pointer.
        instance = this;
        run_audio_loop(audioPool, AudioEngine::audioCallback);
    }

private:
    void processNextBlock(int16_t* output, uint32_t frames) {
        // Clear the CHOC float buffer.
        choc::buffer::setAllFrames(floatView, [](){ return 0.0f; });
        // Process all modules.
        for (auto module : modules)
            module->process(floatView);
        // Convert the CHOC float mix to int16.
        for (uint32_t i = 0; i < floatBuffer.size(); ++i) {
            float s = floatBuffer[i];
            s = std::max(-1.0f, std::min(1.0f, s));
            output[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

    int numChannels, numFrames;
    std::vector<float> floatBuffer;
    choc::buffer::InterleavedView<float> floatView;
    std::vector<AudioModule*> modules;
    struct audio_buffer_pool* audioPool;

    // Singleton pointer used by the static callback.
    inline static AudioEngine* instance = nullptr;
};
