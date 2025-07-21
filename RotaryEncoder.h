/**
 * RotaryEncoder.h - Hardware Rotary Encoder Interface with Button
 * 
 * Implements interrupt-driven rotary encoder reading with integrated push button.
 * Uses quadrature decoding to detect rotation direction and magnitude with
 * debouncing and critical section protection for thread safety.
 * 
 * Hardware Requirements:
 * - Quadrature rotary encoder (2-pin A/B outputs)
 * - Integrated push button (or separate button)
 * - Pull-up resistors (internal pull-ups used)
 * 
 * Features:
 * - Interrupt-driven: No polling required, captures all encoder events
 * - Quadrature decoding: Accurate direction detection and step counting
 * - Button debouncing: Reliable press detection
 * - Thread-safe: Critical sections protect shared state
 * - Multiple instance support: Static management for hardware ISR requirements
 * 
 * Usage Pattern:
 * 1. Create encoder instance with pin assignments
 * 2. Call update() regularly from main loop  
 * 3. Process returned Action and value_change
 */

#pragma once

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/critical_section.h"
#include <cstdio>

/**
 * Interrupt-driven rotary encoder with integrated button support
 * 
 * Technical Details:
 * - Quadrature Encoding: Uses both A and B pins to detect direction
 * - Gray Code Decoding: Reliable step detection with noise immunity
 * - ISR Processing: All encoder logic happens in GPIO interrupt handler
 * - Critical Sections: Protect shared variables between ISR and main thread
 * - Static Management: Hardware ISRs require static callback, single encoder supported
 */
class RotaryEncoder {
public:
    enum class Action { NONE, ROTATED, PRESSED };
    struct UpdateResult { Action action = Action::NONE; int value_change = 0; };

    RotaryEncoder(uint pin_a, uint pin_b, uint pin_sw) {
        if (!s_is_initialized) {
            // Store the pin numbers statically so the ISR can see them
            s_pin_a = pin_a;
            s_pin_b = pin_b;
            s_pin_sw = pin_sw;

            critical_section_init(&s_critical_section);
            // ... init all the GPIOs using the static variables ...
            gpio_init(s_pin_a); gpio_set_dir(s_pin_a, GPIO_IN); gpio_pull_up(s_pin_a);
            gpio_init(s_pin_b); gpio_set_dir(s_pin_b, GPIO_IN); gpio_pull_up(s_pin_b);
            gpio_init(s_pin_sw); gpio_set_dir(s_pin_sw, GPIO_IN); gpio_pull_up(s_pin_sw);

            s_last_ab_state = (gpio_get(s_pin_a) << 1) | gpio_get(s_pin_b);

            gpio_set_irq_enabled_with_callback(s_pin_a, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
            gpio_set_irq_enabled(s_pin_b, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
            gpio_set_irq_enabled(s_pin_sw, GPIO_IRQ_EDGE_FALL, true);

            s_is_initialized = true;
        }
    }

    UpdateResult update() {
        int delta_copy;
        bool button_event_copy;

        critical_section_enter_blocking(&s_critical_section);
        delta_copy = s_encoder_delta; s_encoder_delta = 0;
        button_event_copy = s_button_press_event; s_button_press_event = false;
        critical_section_exit(&s_critical_section);

        if (button_event_copy) {
            printf("LOG:--- PRESSED FIRED ---\n"); // Keep your debug print
            return { Action::PRESSED, 0 };
        }

        if (delta_copy != 0) {
            accumulator += delta_copy;
            int total_change = 0;
            while (accumulator >= 4) { total_change++; accumulator -= 4; }
            while (accumulator <= -4) { total_change--; accumulator += 4; }
            if (total_change != 0) {
                return { Action::ROTATED, total_change };
            }
        }
        return { Action::NONE, 0 };
    }

private:
    static void gpio_callback(uint gpio, uint32_t events) {
        const uint SWITCH_DEBOUNCE_US = 250000;

        // Check against the stored static pin numbers, NOT hardcoded values
        if (gpio == s_pin_a || gpio == s_pin_b) {
            uint8_t current_ab_state = (gpio_get(s_pin_a) << 1) | gpio_get(s_pin_b);
            if (current_ab_state == s_last_ab_state) return;

            uint8_t index = (s_last_ab_state << 2) | current_ab_state;
            int8_t change = s_encoder_states[index];

            if (change != 0) {
                critical_section_enter_blocking(&s_critical_section);
                s_encoder_delta += change;
                critical_section_exit(&s_critical_section);
            }
            s_last_ab_state = current_ab_state;

        } else if (gpio == s_pin_sw) {
            static absolute_time_t last_press_time = {0};
            if (events & GPIO_IRQ_EDGE_FALL) {
                if (absolute_time_diff_us(last_press_time, get_absolute_time()) < SWITCH_DEBOUNCE_US) return;
                last_press_time = get_absolute_time();
                critical_section_enter_blocking(&s_critical_section);
                s_button_press_event = true;
                critical_section_exit(&s_critical_section);
            }
        }
    }

    // Static variables to hold pin numbers for the ISR
    inline static uint s_pin_a, s_pin_b, s_pin_sw;

    inline static critical_section_t s_critical_section;
    inline static volatile int s_encoder_delta = 0;
    inline static volatile bool s_button_press_event = false;
    inline static volatile uint8_t s_last_ab_state = 0;
    inline static bool s_is_initialized = false;
    inline static const int8_t s_encoder_states[] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};

    int accumulator = 0;
};