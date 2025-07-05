#pragma once

#include "RotaryEncoder.h"
#include "ParameterStore.h"
#include "pico/stdlib.h"
#include <cstdio>

class RotaryEncoderListener {
public:
    RotaryEncoderListener() : encoder(13, 14, 15) { // Use the new pins
        printf("LOG:Rotary Encoder Ready. Controlling %zu parameters.\n", g_synth_parameters.size());
        print_selection();
    }

    void update() {
        auto result = encoder.update();

        // Use two separate 'if' statements to handle simultaneous press/rotate events.
        if (result.action == RotaryEncoder::Action::PRESSED) {
            active_parameter_index = (active_parameter_index + 1) % g_synth_parameters.size();
            print_selection();
        }

        if (result.action == RotaryEncoder::Action::ROTATED) {
            if (g_synth_parameters.empty()) return;
            Parameter* p = g_synth_parameters[active_parameter_index];
            float new_norm = p->getNormalizedValue() + (result.value_change * 0.008f);
            p->setNormalizedValue(new_norm);
            broadcast_parameter_state(p);
        }
    }

private:
    void print_selection() {
        if (g_synth_parameters.empty()) return;
        Parameter* p = g_synth_parameters[active_parameter_index];
        printf("SELECT:%s\n", p->getName().c_str());
        fflush(stdout);
    }

    void broadcast_parameter_state(const Parameter* p) {
        printf("STATE:%d:%.3f\n", p->getCcNumber(), p->getNormalizedValue());
        fflush(stdout);
    }

    RotaryEncoder encoder;
    int active_parameter_index = 0;
};