//
// Created by charTP on 21/02/2025.
//
#pragma once
#include <cassert>
#include <cmath>
#include <algorithm>

/**
    A simple linear smoothed value class.

    This class allows you to set a target value and then step smoothly toward that target
    over a specified number of samples. It is intended for real-time use (e.g. for smoothing
    parameter changes) and avoids dynamic memory allocation.

    Note: This simple version is not thread-safe; it is assumed that smoothing is performed
    on the audio thread.
*/
template <typename FloatType>
class SmoothedValue
{
public:
    SmoothedValue() noexcept
        : currentValue(0), targetValue(0), step(0), remainingSamples(0), rampSamples(0)
    {
    }

    explicit SmoothedValue(FloatType initialValue) noexcept
        : currentValue(initialValue), targetValue(initialValue), step(0), remainingSamples(0), rampSamples(0)
    {
    }

    /// Sets the ramp length (in samples) for future transitions
    void reset(int numSamples) noexcept
    {
        rampSamples = numSamples;
        // Don't change currentValue or targetValue here!
    }

    /// Sets the ramp length using seconds
    void reset(double sampleRate, double rampLengthInSeconds) noexcept
    {
        int numSamples = static_cast<int>(std::round(sampleRate * rampLengthInSeconds));
        reset(numSamples);
    }

    /// Sets both current and target to the same value (for initialization)
    void setValue(FloatType value) noexcept
    {
        currentValue = value;
        targetValue = value;
        remainingSamples = 0;
        step = 0;
    }

    /// Sets a new target value and starts ramping towards it
    void setTargetValue(FloatType newValue) noexcept
    {
        if (newValue == targetValue) return; // No change needed

        targetValue = newValue;
        remainingSamples = rampSamples;

        if (remainingSamples > 0)
            step = (targetValue - currentValue) / static_cast<FloatType>(remainingSamples);
        else
            currentValue = targetValue;
    }

    /// Advances the smoothed value by one sample and returns it.
    FloatType getNextValue() noexcept
    {
        if (remainingSamples > 0)
        {
            currentValue += step;
            --remainingSamples;

            // Clamp to target on final sample to avoid floating point drift
            if (remainingSamples == 0)
                currentValue = targetValue;
        }
        return currentValue;
    }

    /// Returns the current value without advancing.
    FloatType getCurrentValue() const noexcept { return currentValue; }

    /// Returns the target value.
    FloatType getTargetValue() const noexcept { return targetValue; }

    /// Returns true if currently ramping
    bool isSmoothing() const noexcept { return remainingSamples > 0; }

private:
    FloatType currentValue, targetValue, step;
    int remainingSamples, rampSamples;
};