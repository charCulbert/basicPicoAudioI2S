#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/multicore.h"

//==============================================================================
// THE SINGLE SOURCE OF TRUTH FOR CC CONTROLS
//==============================================================================
struct CcMapping {
    uint8_t cc_number;
    const char* name;
};

inline const std::vector<CcMapping> g_ccMappings = {
    {1,  "Mod Index"},
    {10, "Harmonicity"},
    {74, "Attack Time"},
    {71, "Decay Time"},
    {73, "Sustain Lvl"},
    {72, "Release Time"},
    {75, "Master Volume"}
};

// Define the MIDI command types needed for sending to Core 1
enum MidiCommandType {
    NOTE_OFF_CMD   = 0x80,
    NOTE_ON_CMD    = 0x90,
    CONTROL_CHANGE_CMD = 0xB0
};


//==============================================================================
// The MidiSerialListener Class - All Implementation is INLINE
//==============================================================================
class MidiSerialListener {
public:
    // The constructor is simple enough to be defined inline directly.
    MidiSerialListener() : ascii_pos(0) {}

    // The main public method. This contains the infinite loop.
    // Marked 'inline' to prevent multiple-definition errors.
    inline void run() {
        while (true) {
            int c = getchar_timeout_us(0);
            if (c == PICO_ERROR_TIMEOUT) continue;

            if (c & 0x80) { // MIDI status byte
                int data1 = getchar(); 
                int data2 = getchar();
                if (data1 != PICO_ERROR_TIMEOUT && data2 != PICO_ERROR_TIMEOUT) {
                    handleMidiMessage(c, data1, data2);
                }
            } else { // ASCII character
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
    }

private:
    // All handling logic is now private and inline.
    inline void handleMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
        uint8_t command = status & 0xF0;
        if (command == 0x90 && data2 > 0)          sendMidiToCore1(NOTE_ON_CMD, data1, data2);
        else if (command == 0x80 || (command == 0x90 && data2 == 0)) sendMidiToCore1(NOTE_OFF_CMD, data1, data2);
        else if (command == 0xB0)           sendMidiToCore1(CONTROL_CHANGE_CMD, data1, data2);
    }

    inline void handleAsciiCommand(const char* buffer) {
        if (strcmp(buffer, "SYNC_KNOBS") == 0) {
            printf("KNOB_UPDATE_START\n");
            for (const auto& mapping : g_ccMappings) {
                printf("CC_DEF:%d:%s\n", mapping.cc_number, mapping.name);
            }
            printf("KNOB_UPDATE_END\n");
        } else {
            printf("Received ASCII Command: %s\n", buffer);
        }
    }

    inline void sendMidiToCore1(uint8_t command, uint8_t data1, uint8_t data2) {
        uint32_t packet = (command << 24) | (data1 << 16) | (data2 << 8);
        multicore_fifo_push_blocking(packet);
    }
    
    // Member variables to hold the state of the ASCII buffer.
    char ascii_buffer[64];
    int ascii_pos;
};