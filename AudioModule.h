//
// Created by char on 3/23/25.
//

#ifndef AUDIOMODULE_H
#define AUDIOMODULE_H
//==============================================================================
// Abstract base class for audio modules.
// Your modules will work entirely with CHOC interleaved float buffers.
class AudioModule {
public:
    virtual ~AudioModule() = default;
    virtual void process(choc::buffer::InterleavedView<float> buffer) = 0;
};

#endif //AUDIOMODULE_H
