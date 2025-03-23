#include <cmath>
#include <cstdint>
#include <vector>
#include <cstdio>

// We'll provide this function so the C code can call it for each block.
extern "C" void fill_audio_block(int16_t* samples, uint32_t numFrames);

// A simple stereo oscillator class for demonstration:
//  - left channel = 1 frequency
//  - right channel = another frequency
class StereoOsc {
public:
    StereoOsc()
    {
        // Build a sine lookup table of 8192 samples
        waveTableSize = 8192;
        waveTable.resize(waveTableSize);

        for (uint32_t i = 0; i < waveTableSize; ++i) {
            float angle = 2.0f * float(M_PI) * float(i) / float(waveTableSize);
            waveTable[i] = (int16_t)(32767 * std::cos(angle));
        }

        // Set up "fixed-point" indexing
        posMax  = waveTableSize << 16;

        // Initialize left channel oscillator
        leftPos   = 0;
        leftStep  = 0x300000;  // controls pitch for left channel
        leftVol   = 32;

        // Initialize right channel oscillator
        rightPos  = 0;
        rightStep = 0x500000;  // different pitch for right channel
        rightVol  = 32;
    }

    // Fill a stereo buffer (interleaved L,R) with numFrames frames
    void fillBlock(int16_t* output, uint32_t numFrames)
    {
        for (uint32_t i = 0; i < numFrames; ++i)
        {
            // Left sample
            int16_t leftRaw = waveTable[leftPos >> 16];
            int16_t leftOut = (int16_t)((leftVol * leftRaw) >> 8);

            // Right sample
            int16_t rightRaw = waveTable[rightPos >> 16];
            int16_t rightOut = (int16_t)((rightVol * rightRaw) >> 8);

            // Write interleaved
            output[2*i + 0] = leftOut;
            output[2*i + 1] = rightOut;

            // Advance left
            leftPos += leftStep;
            if (leftPos >= posMax)
                leftPos -= posMax;

            // Advance right
            rightPos += rightStep;
            if (rightPos >= posMax)
                rightPos -= posMax;
        }
    }

private:
    // The shared sine wave table
    std::vector<int16_t> waveTable;
    uint32_t waveTableSize;
    uint32_t posMax;

    // Left channel oscillator state
    uint32_t leftPos;
    uint32_t leftStep;
    uint16_t leftVol;

    // Right channel oscillator
    uint32_t rightPos;
    uint32_t rightStep;
    uint16_t rightVol;
};

// We'll have one global StereoOsc instance.
static StereoOsc gStereoOsc;

// The function that the C code calls
extern "C" void fill_audio_block(int16_t* samples, uint32_t numFrames)
{
    // Just pass it to our global oscillator
    gStereoOsc.fillBlock(samples, numFrames);
}
