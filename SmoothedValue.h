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
        : currentValue(0), targetValue(0), step(0), remainingSamples(0)
    {
    }

    explicit SmoothedValue(FloatType initialValue) noexcept
        : currentValue(initialValue), targetValue(initialValue), step(0), remainingSamples(0)
    {
    }

    /// Resets the ramp length (in samples) and sets the current value to the target.
    void reset(int numSamples) noexcept
    {
        remainingSamples = numSamples;
        currentValue = targetValue;
        step = 0;
    }

    /// Resets using a ramp length in seconds.
    void reset(double sampleRate, double rampLengthInSeconds) noexcept
    {
        int numSamples = static_cast<int>(std::round(sampleRate * rampLengthInSeconds));
        reset(numSamples);
    }

    /// Sets a new target value and calculates the step based on the current ramp length.
    void setTargetValue(FloatType newValue) noexcept
    {
        targetValue = newValue;
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
        }
        else
        {
            currentValue = targetValue;
        }
        return currentValue;
    }

    /// Returns the current value without advancing.
    FloatType getCurrentValue() const noexcept { return currentValue; }

    /// Returns the target value.
    FloatType getTargetValue() const noexcept { return targetValue; }

private:
    FloatType currentValue, targetValue, step;
    int remainingSamples;
};
