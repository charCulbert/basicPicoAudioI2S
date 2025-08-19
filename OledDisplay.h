#pragma once

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include <string>

class OledDisplay {
public:
    static const int SCREEN_WIDTH = 128;
    static const int SCREEN_HEIGHT = 64;
    static const int BUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT / 8;
    
    OledDisplay(uint sda_pin = 4, uint scl_pin = 5, uint8_t i2c_addr = 0x3C);
    
    bool init();
    void clear();
    void display();
    bool displayAsync();  // Non-blocking display update
    bool isDisplayBusy(); // Check if DMA transfer is in progress
    void writeText(const std::string& text, int16_t x = 0, int16_t y = 0);
    void setPixel(int x, int y, bool on = true);
    void drawLine(int x0, int y0, int x1, int y1, bool on = true);
    
    void invertDisplay(bool invert);
    void startScrolling();
    void stopScrolling();
    void setBrightness(uint8_t brightness);
    void sleep(bool enable);
    
private:
    uint sda_pin_;
    uint scl_pin_;
    uint8_t i2c_addr_;
    uint8_t buffer_[BUFFER_SIZE];
    uint8_t dma_buffer_[BUFFER_SIZE + 1]; // DMA buffer with command byte
    bool initialized_;
    int dma_chan_;        // DMA channel for I2C transfers
    bool display_busy_;   // Track DMA transfer state
    
    void sendCommand(uint8_t cmd);
    void sendBuffer();
    void writeChar(int16_t x, int16_t y, uint8_t ch);
};

void writeToOled(const std::string& text, int16_t x = 0, int16_t y = 0);
void clearOled();
void invertOled(bool invert);
void startScrollOled();
void stopScrollOled();