/**
 * Parameter.h - Thread-Safe Audio Parameter System
 * 
 * Implements thread-safe parameters for real-time audio applications using
 * atomic operations. Parameters can be safely updated from control threads
 * while being read from the audio thread without locks or blocking.
 * 
 * Key Features:
 * - Lock-free atomic operations for thread safety
 * - MIDI CC number association for hardware/UI control
 * - Automatic range clamping and validation
 * - Normalized [0,1] interface for UI/MIDI (0-127) integration
 * - String ID system for parameter lookup
 * 
 * Thread Model:
 * - Control Thread: Updates parameters via setValue() or setNormalizedValue()
 * - Audio Thread: Reads parameters via getValue() (lock-free, real-time safe)
 * - UI/MIDI: Uses normalized values [0,1] mapped to MIDI CC [0,127]
 */

#pragma once
#include <atomic>
#include <string>
#include <cassert>
#include <algorithm>

// Forward declaration
void showSynthParameter(const std::string& name, float value);
/**
 * Thread-safe parameter class for real-time audio applications
 * 
 * Each parameter has:
 * - Physical range: [minimum, maximum] in actual units (Hz, seconds, etc.)
 * - Normalized range: [0.0, 1.0] for UI/MIDI control
 * - MIDI CC number: Links parameter to specific MIDI controller
 * - String ID: Human-readable identifier for parameter lookup
 * 
 * Thread Safety:
 * - Uses std::atomic<float> for lock-free access
 * - Control thread can update anytime via setValue()/setNormalizedValue()
 * - Audio thread reads via getValue() without blocking or interruption
 * - Memory ordering: relaxed (sufficient for audio parameter updates)
 */
class Parameter {
public:
    /**
     * Construct a parameter with full specification
     * @param id - Unique string identifier for parameter lookup
     * @param name - Human-readable display name
     * @param minValue - Minimum value in physical units
     * @param maxValue - Maximum value in physical units  
     * @param defaultValue - Initial value (will be clamped to range)
     * @param midiCcNumber - MIDI CC number (0-127) for hardware control
     */
    Parameter(const std::string& id,
              const std::string& name,
              float minValue,
              float maxValue,
              float defaultValue,
              uint8_t midiCcNumber)
        : parameterID(id), displayName(name),
          minimum(minValue), maximum(maxValue),
          value(defaultValue),
          ccNumber(midiCcNumber)
    {
        assert(minValue < maxValue);
        // Clamp default value to valid range
        if (defaultValue < minValue) value.store(minValue, std::memory_order_relaxed);
        if (defaultValue > maxValue) value.store(maxValue, std::memory_order_relaxed);
    }

    /**
     * Set parameter value in physical units (thread-safe)
     * Automatically clamps to [minimum, maximum] range
     * @param newValue - New value in physical units
     */
    void setValue(float newValue) {
        newValue = std::max(minimum, std::min(newValue, maximum));
        value.store(newValue, std::memory_order_relaxed);
        // Screen update moved to setNormalizedValue() to avoid duplicate calls
    }

    /**
     * Get current parameter value in physical units (thread-safe)
     * Safe to call from audio thread - no blocking or locks
     * @return Current value in physical units
     */
    float getValue() const {
        return value.load(std::memory_order_relaxed);
    }

    /**
     * Get parameter value as normalized [0,1] value
     * Used by UI and MIDI systems for consistent scaling
     * @return Value mapped to [0,1] range
     */
    float getNormalizedValue() const {
        return (getValue() - minimum) / (maximum - minimum);
    }

    /**
     * Set parameter using normalized [0,1] value (thread-safe)
     * Converts normalized value to physical range
     * Used by MIDI CC (0-127 mapped to 0-1) and UI controls
     * @param norm - Normalized value [0,1] (will be clamped)
     */
    void setNormalizedValue(float norm) {
        norm = std::max(0.0f, std::min(norm, 1.0f));
        setValue(minimum + norm * (maximum - minimum));
        
        // Skip screen updates during rapid parameter changes for better responsiveness
        static uint32_t last_screen_update = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        if ((now - last_screen_update) > 100) {  // Only update screen every 100ms max
            showSynthParameter(this->parameterID, this->getNormalizedValue());
            last_screen_update = now;
        }
    }

    // === Accessors for Parameter Metadata ===
    const std::string& getID() const { return parameterID; }
    const std::string& getName() const { return displayName; }
    float getMinimum() const { return minimum; }
    float getMaximum() const { return maximum; }
    uint8_t getCcNumber() const { return ccNumber; }

private:
    // === Parameter Metadata ===
    std::string parameterID;        // Unique identifier for parameter lookup
    std::string displayName;        // Human-readable name for UI display
    float minimum, maximum;         // Physical value range boundaries
    uint8_t ccNumber;              // MIDI CC number for hardware control
    
    // === Thread-Safe Value Storage ===
    std::atomic<float> value;       // Current parameter value (lock-free atomic)
};
