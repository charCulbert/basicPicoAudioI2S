#pragma once
#include <atomic>
#include <string>
#include <cassert>
#include <algorithm>

class Parameter {
public:
    // *** MODIFIED CONSTRUCTOR *** to accept a CC number
    Parameter(const std::string& id,
              const std::string& name,
              float minValue,
              float maxValue,
              float defaultValue,
              uint8_t midiCcNumber) // <-- ADD THIS
        : parameterID(id), displayName(name),
          minimum(minValue), maximum(maxValue),
          value(defaultValue),
          ccNumber(midiCcNumber) // <-- ADD THIS
    {
        assert(minValue < maxValue);
        if (defaultValue < minValue) value.store(minValue, std::memory_order_relaxed);
        if (defaultValue > maxValue) value.store(maxValue, std::memory_order_relaxed);
    }

    void setValue(float newValue) {
        newValue = std::max(minimum, std::min(newValue, maximum));
        value.store(newValue, std::memory_order_relaxed);
    }

    float getValue() const {
        return value.load(std::memory_order_relaxed);
    }

    float getNormalizedValue() const {
        return (getValue() - minimum) / (maximum - minimum);
    }

    void setNormalizedValue(float norm) {
        norm = std::max(0.0f, std::min(norm, 1.0f));
        setValue(minimum + norm * (maximum - minimum));
    }

    const std::string& getID() const { return parameterID; }
    const std::string& getName() const { return displayName; }
    float getMinimum() const { return minimum; }
    float getMaximum() const { return maximum; }
    uint8_t getCcNumber() const { return ccNumber; } // <-- ADD THIS GETTER

private:
    std::string parameterID;
    std::string displayName;
    float minimum;
    float maximum;
    std::atomic<float> value;
    uint8_t ccNumber; // <-- ADD THIS MEMBER
};