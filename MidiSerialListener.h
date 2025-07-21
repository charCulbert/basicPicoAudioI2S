/**
 * MidiSerialListener.h - USB Serial MIDI and Command Interface
 * 
 * Handles incoming data from USB serial port, supporting both:
 * 1. MIDI messages (binary protocol) from hardware controllers and software
 * 2. ASCII commands (text protocol) from the HTML control interface
 * 
 * MIDI Protocol Support:
 * - Note On/Off messages: Forwarded to audio thread via multicore FIFO
 * - Continuous Controller (CC): Updates global parameter store
 * - Automatic MIDI CC to parameter mapping via parameter CC numbers
 * 
 * ASCII Command Support:
 * - "SYNC_KNOBS": Sends all parameter definitions and values to HTML UI
 * - Line-based protocol (commands end with \n or \r)
 * 
 * Threading Model:
 * - Runs on control thread (Core 0) in main loop
 * - Updates parameter store (thread-safe atomic operations)
 * - Sends MIDI note events to audio thread (Core 1) via FIFO
 * - Non-blocking serial reads prevent audio thread interference
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "ParameterStore.h"

// MIDI command types for inter-core communication
enum MidiCommandType { NOTE_OFF_CMD = 0x80, NOTE_ON_CMD = 0x90, ALL_NOTES_OFF_CMD = 0xB0 };

/**
 * Dual-protocol serial interface handler
 * 
 * Automatically detects and handles both MIDI (binary) and ASCII (text) data
 * from the same USB serial connection. This allows a single connection to support:
 * - Web browser HTML interface (ASCII commands)
 * - MIDI controllers and software (binary MIDI)
 * - External hardware controllers via USB-MIDI
 */
class MidiSerialListener {
public:
    MidiSerialListener() : ascii_pos(0) {}
    
    /**
     * Process incoming serial data (call from main control loop)
     * 
     * Non-blocking function that:
     * 1. Checks for available serial data
     * 2. Detects MIDI vs ASCII protocols automatically
     * 3. Routes data to appropriate handler
     * 
     * Protocol Detection:
     * - MIDI: Bytes with high bit set (0x80-0xFF) indicate MIDI status
     * - ASCII: Regular text characters (0x00-0x7F) for commands
     */
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
            // Handle All Notes Off (CC 123)
            if (data1 == 123) {
                sendAllNotesOffToCore1();
                printf("LOG:All Notes Off\n");
                fflush(stdout);
                return;
            }
            
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
    
    void sendAllNotesOffToCore1() {
        uint32_t packet = (ALL_NOTES_OFF_CMD << 24) | (123 << 16) | (0 << 8);
        multicore_fifo_push_blocking(packet);
    }

    char ascii_buffer[64];
    int ascii_pos;
};