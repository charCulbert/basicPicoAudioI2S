/**
 * AudioModule.h - Abstract Base Class for Audio Processing Modules
 * 
 * This defines the common interface that all audio processing modules must implement.
 * All audio processing uses 16.15 fixed-point arithmetic for optimal performance on
 * the RP2040 microcontroller (which has no floating-point unit).
 */

#ifndef AUDIOMODULE_H
#define AUDIOMODULE_H

#include "Fix15.h"
#include "choc/audio/choc_SampleBuffers.h"

//==============================================================================
/**
 * Abstract base class for all audio processing modules in the system.
 * 
 * Key Design Principles:
 * - Uses fix15 (16.15 fixed-point) arithmetic for all audio processing
 * - Works with CHOC interleaved buffers for efficient memory layout
 * - Pure virtual interface allows polymorphic audio processing chains
 * - Real-time safe: no memory allocation in process() calls
 * 
 * Usage:
 * 1. Inherit from AudioModule
 * 2. Implement the process() method to handle audio buffers
 * 3. Add your module to an AudioEngine for processing
 */
class AudioModule {
public:
    virtual ~AudioModule() = default;
    
    /**
     * Process an audio buffer in real-time.
     * 
     * @param buffer - Interleaved fix15 audio buffer (e.g., L,R,L,R for stereo)
     *                 Buffer contains getNumFrames() frames and getNumChannels() channels
     *                 
     * CRITICAL: This method runs in the real-time audio thread.
     * - NO memory allocation
     * - NO blocking operations (mutex, sleep, etc.)
     * - NO printf/stdout (use only for debugging, not production)
     * - Keep processing time predictable and minimal
     */
    virtual void process(choc::buffer::InterleavedView<fix15>& buffer) = 0;
};

#endif // AUDIOMODULE_H
