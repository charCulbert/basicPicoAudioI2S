//
// Created by charTP on 21/02/2025.
//
#pragma once
#include <cassert>
#include <cmath>
#include <algorithm>
#include "Fix15.h"
#include "pico/multicore.h" // For memory barriers

/**
    A simple linear smoothed value class.

    This class allows you to set a target value and then step smoothly toward that target
    over a specified number of samples. It is intended for real-time use (e.g. for smoothing
    parameter changes) and avoids dynamic memory allocation.

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
    /// Thread-safe: can be called from control thread while audio thread calls getNextValue()
    void setTargetValue(FloatType newValue) noexcept
    {
        if (newValue == targetValue) return; // No change needed

        targetValue = newValue;
        
        // Always start a fresh ramp from current position to new target
        remainingSamples = rampSamples;

        if (remainingSamples > 0)
            step = (targetValue - currentValue) / static_cast<FloatType>(remainingSamples);
        else
            currentValue = targetValue;
    }

    /// Advances the smoothed value by one sample and returns it.
    /// Thread-safe: can be called from audio thread while control thread calls setTargetValue()
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
    volatile int remainingSamples; // volatile for basic thread safety on dual-core
    int rampSamples;
};

//==============================================================================
// Explicit fix15 version with thread safety for dual-core Pico
//==============================================================================

class Fix15SmoothedValue
{
public:
    Fix15SmoothedValue() noexcept
        : currentValue(FIX15_ZERO), targetValue(FIX15_ZERO), step(FIX15_ZERO), 
          remainingSamples(0), rampSamples(0), pendingTarget(FIX15_ZERO), hasNewTarget(false)
    {
    }

    explicit Fix15SmoothedValue(fix15 initialValue) noexcept
        : currentValue(initialValue), targetValue(initialValue), step(FIX15_ZERO), 
          remainingSamples(0), rampSamples(0), pendingTarget(initialValue), hasNewTarget(false)
    {
    }

    /// Sets the ramp length (in samples) for future transitions
    void reset(int numSamples) noexcept
    {
        rampSamples = numSamples;
    }

    /// Sets the ramp length using seconds
    void reset(double sampleRate, double rampLengthInSeconds) noexcept
    {
        int numSamples = static_cast<int>(std::round(sampleRate * rampLengthInSeconds));
        reset(numSamples);
    }

    /// Sets both current and target to the same value (for initialization)
    void setValue(fix15 value) noexcept
    {
        currentValue = value;
        targetValue = value;
        remainingSamples = 0;
        step = FIX15_ZERO;
    }
    
    /// Convenience method to set value from float
    void setValue(float value) noexcept
    {
        setValue(float2fix15(value));
    }

    /// Sets a new target value - LOCKLESS, called from control thread
    /// Audio thread will pick up the change on next buffer
    void setTargetValue(fix15 newValue) noexcept
    {
        // Pico SDK lockless: Control thread writes, audio thread reads
        pendingTarget = newValue;
        __dmb(); // Data memory barrier - ensures write completes before flag
        hasNewTarget = true;
    }
    
    /// Convenience method to set target from float
    void setTargetValue(float value) noexcept
    {
        setTargetValue(float2fix15(value));
    }

    /// Advances the smoothed value by one sample and returns it.
    /// LOCKLESS: Called from audio thread, never blocks
    fix15 getNextValue() noexcept
    {
        // Check for new target from control thread (lockless read)
        if (hasNewTarget)
        {
            fix15 newTarget = pendingTarget;
            __dmb(); // Data memory barrier - ensures read completes before clearing flag
            hasNewTarget = false; // Acknowledge we got it
            
            // Start fresh ramp from current position to new target
            targetValue = newTarget;
            remainingSamples = rampSamples;
            
            if (remainingSamples > 0)
            {
                fix15 diff = targetValue - currentValue;
                step = divfix15(diff, int2fix15(remainingSamples));
            }
            else
            {
                currentValue = targetValue;
                step = FIX15_ZERO;
            }
        }
        
        // Continue smoothing
        if (remainingSamples > 0)
        {
            currentValue += step;
            --remainingSamples;

            if (remainingSamples == 0)
                currentValue = targetValue;
        }
        
        return currentValue;
    }

    /// Returns the current value without advancing.
    fix15 getCurrentValue() const noexcept { return currentValue; }

    /// Returns the target value.
    fix15 getTargetValue() const noexcept { return targetValue; }

    /// Returns true if currently ramping
    bool isSmoothing() const noexcept { return remainingSamples > 0; }

private:
    // Audio thread state (only accessed by audio thread)
    fix15 currentValue, targetValue, step;
    int remainingSamples;
    int rampSamples;
    
    // Lockless communication between threads (using RP2040's memory barriers)
    volatile fix15 pendingTarget;
    volatile bool hasNewTarget;
};