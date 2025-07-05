#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "ParameterStore.h"

enum MidiCommandType { NOTE_OFF_CMD = 0x80, NOTE_ON_CMD = 0x90 };

class MidiSerialListener {
public:
    MidiSerialListener() : ascii_pos(0) {}
    void update() {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        if (c & 0x80) {
            int data1 = getchar(); int data2 = getchar();
            if (data1 != PICO_ERROR_TIMEOUT && data2 != PICO_ERROR_TIMEOUT) {
                handleMidiMessage(c, data1, data2);
            }
        } else {
            if (c == '\n' || c == '\r') {
                if (ascii_pos > 0) {
                    ascii_buffer[ascii_pos] = '\0';
                    handleAsciiCommand(ascii_buffer);
                    ascii_pos = 0;
                }
            } else if (ascii_pos < sizeof(ascii_buffer) - 1) {
                ascii_buffer[ascii_pos++] = c;
            }
        }
    }

private:
    void handleMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
        uint8_t command = status & 0xF0;
        if (command == 0x90 && data2 > 0) sendNoteToCore1(NOTE_ON_CMD, data1, data2);
        else if (command == 0x80 || (command == 0x90 && data2 == 0)) sendNoteToCore1(NOTE_OFF_CMD, data1, data2);
        else if (command == 0xB0) {
            for (auto* p : g_synth_parameters) {
                if (p->getCcNumber() == data1) {
                    p->setNormalizedValue(data2 / 127.0f);
                    printf("STATE:%d:%.3f\n", p->getCcNumber(), p->getNormalizedValue());
                    fflush(stdout);
                    break;
                }
            }
        }
    }

    void handleAsciiCommand(const char* buffer) {
        if (strcmp(buffer, "SYNC_KNOBS") == 0) {
            printf("KNOB_UPDATE_START\n");
            // 1. Send all the definitions
            for (const auto* p : g_synth_parameters) {
                printf("CC_DEF:%d:%s\n", p->getCcNumber(), p->getName().c_str());
            }
            // *** NEW FEATURE: Send all the current values ***
            for (const auto* p : g_synth_parameters) {
                printf("STATE:%d:%.3f\n", p->getCcNumber(), p->getNormalizedValue());
            }
            printf("KNOB_UPDATE_END\n");
            fflush(stdout);
        } else {
            printf("LOG:Received ASCII Command: %s\n", buffer);
        }
    }

    void sendNoteToCore1(uint8_t command, uint8_t data1, uint8_t data2) {
        uint32_t packet = (command << 24) | (data1 << 16) | (data2 << 8);
        multicore_fifo_push_blocking(packet);
    }

    char ascii_buffer[64];
    int ascii_pos;
};