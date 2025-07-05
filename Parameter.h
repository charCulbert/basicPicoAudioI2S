//
// Created by charTP on 21/02/2025.
//

#pragma once
#include <atomic>
#include <string>
#include <cassert>
#include <algorithm>

// A simple, thread-safe parameter class.
class Parameter
{
public:
    /**
      Constructs a Parameter with the given properties.

      @param id           A unique identifier (no spaces).
      @param name         A human-readable name.
      @param minValue     The minimum allowed value.
      @param maxValue     The maximum allowed value.
      @param defaultValue The default value; should be between minValue and maxValue.
    */
    Parameter(const std::string& id,
              const std::string& name,
              float minValue,
              float maxValue,
              float defaultValue)
        : parameterID(id), displayName(name),
          minimum(minValue), maximum(maxValue),
          value(defaultValue)
    {
        assert(minValue < maxValue);
        // Clamp the default value.
        if (defaultValue < minValue)
            value.store(minValue, std::memory_order_relaxed);
        if (defaultValue > maxValue)
            value.store(maxValue, std::memory_order_relaxed);
    }

    /// Sets the parameter value, clamping it between the minimum and maximum.
    void setValue(float newValue)
    {
        newValue = std::max(minimum, std::min(newValue, maximum));
        value.store(newValue, std::memory_order_relaxed);
    }

    /// Returns the current parameter value.
    float getValue() const
    {
        return value.load(std::memory_order_relaxed);
    }

    /// Returns the normalized value in the range [0, 1].
    float getNormalizedValue() const
    {
        return (getValue() - minimum) / (maximum - minimum);
    }

    /// Sets the parameter value using a normalized value in the range [0, 1].
    void setNormalizedValue(float norm)
    {
        norm = std::max(0.0f, std::min(norm, 1.0f));
        setValue(minimum + norm * (maximum - minimum));
    }

    const std::string& getID() const { return parameterID; }
    const std::string& getName() const { return displayName; }
    float getMinimum() const { return minimum; }
    float getMaximum() const { return maximum; }

private:
    std::string parameterID;
    std::string displayName;
    float minimum;
    float maximum;
    std::atomic<float> value;
};
