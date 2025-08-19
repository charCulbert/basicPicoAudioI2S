#include "OledDisplay.h"
#include "ssd1306_font.h"
#include <cstring>
#include <cctype>
#include <algorithm>
#include "hardware/dma.h"

#define SSD1306_SET_MEM_MODE        0x20
#define SSD1306_SET_COL_ADDR        0x21
#define SSD1306_SET_PAGE_ADDR       0x22
#define SSD1306_SET_HORIZ_SCROLL    0x26
#define SSD1306_SET_SCROLL          0x2E

#define SSD1306_SET_DISP_START_LINE 0x40
#define SSD1306_SET_CONTRAST        0x81
#define SSD1306_SET_CHARGE_PUMP     0x8D

#define SSD1306_SET_SEG_REMAP       0xA0
#define SSD1306_SET_ENTIRE_ON       0xA4
#define SSD1306_SET_NORM_DISP       0xA6
#define SSD1306_SET_INV_DISP        0xA7
#define SSD1306_SET_MUX_RATIO       0xA8
#define SSD1306_SET_DISP            0xAE
#define SSD1306_SET_COM_OUT_DIR     0xC0

#define SSD1306_SET_DISP_OFFSET     0xD3
#define SSD1306_SET_DISP_CLK_DIV    0xD5
#define SSD1306_SET_PRECHARGE       0xD9
#define SSD1306_SET_COM_PIN_CFG     0xDA
#define SSD1306_SET_VCOM_DESEL      0xDB

#define SSD1306_NUM_PAGES           (SCREEN_HEIGHT / 8)

OledDisplay::OledDisplay(uint sda_pin, uint scl_pin, uint8_t i2c_addr)
    : sda_pin_(sda_pin), scl_pin_(scl_pin), i2c_addr_(i2c_addr), initialized_(false), 
      dma_chan_(-1), display_busy_(false) {
    memset(buffer_, 0, BUFFER_SIZE);
    memset(dma_buffer_, 0, BUFFER_SIZE + 1);
}

bool OledDisplay::init() {
    i2c_init(i2c0, 400000);
    gpio_set_function(sda_pin_, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin_, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin_);
    gpio_pull_up(scl_pin_);
    
    sleep_ms(100);
    
    uint8_t init_cmds[] = {
        SSD1306_SET_DISP,               // display off
        SSD1306_SET_MEM_MODE,           // set memory address mode
        0x00,                           // horizontal addressing mode
        SSD1306_SET_DISP_START_LINE,    // set display start line to 0
        SSD1306_SET_SEG_REMAP | 0x01,   // set segment re-map
        SSD1306_SET_MUX_RATIO,          // set multiplex ratio
        SCREEN_HEIGHT - 1,              // display height - 1
        SSD1306_SET_COM_OUT_DIR | 0x08, // set COM output scan direction
        SSD1306_SET_DISP_OFFSET,        // set display offset
        0x00,                           // no offset
        SSD1306_SET_COM_PIN_CFG,        // set COM pins hardware config
        0x12,                           // 128x64 config
        SSD1306_SET_DISP_CLK_DIV,       // set display clock divide ratio
        0x80,                           // standard freq
        SSD1306_SET_PRECHARGE,          // set pre-charge period
        0xF1,                           // Vcc internally generated
        SSD1306_SET_VCOM_DESEL,         // set VCOMH deselect level
        0x30,                           // 0.83xVcc
        SSD1306_SET_CONTRAST,           // set contrast control
        0xFF,                           // max contrast
        SSD1306_SET_ENTIRE_ON,          // set entire display on to follow RAM
        SSD1306_SET_NORM_DISP,          // set normal display
        SSD1306_SET_CHARGE_PUMP,        // set charge pump
        0x14,                           // Vcc internally generated
        SSD1306_SET_SCROLL | 0x00,      // deactivate scrolling
        SSD1306_SET_DISP | 0x01         // display on
    };
    
    for (uint8_t cmd : init_cmds) {
        sendCommand(cmd);
    }
    
    // Initialize DMA channel for non-blocking I2C transfers
    dma_chan_ = dma_claim_unused_channel(true);
    
    initialized_ = true;
    clear();
    display();
    
    return true;
}

void OledDisplay::clear() {
    memset(buffer_, 0, BUFFER_SIZE);
}

void OledDisplay::display() {
    if (!initialized_) return;
    
    uint8_t cmds[] = {
        SSD1306_SET_COL_ADDR, 0, SCREEN_WIDTH - 1,
        SSD1306_SET_PAGE_ADDR, 0, SSD1306_NUM_PAGES - 1
    };
    
    for (uint8_t cmd : cmds) {
        sendCommand(cmd);
    }
    
    sendBuffer();
}

bool OledDisplay::displayAsync() {
    if (!initialized_ || display_busy_) return false;
    
    // Set up display area commands (blocking - very fast)
    uint8_t cmds[] = {
        SSD1306_SET_COL_ADDR, 0, SCREEN_WIDTH - 1,
        SSD1306_SET_PAGE_ADDR, 0, SSD1306_NUM_PAGES - 1
    };
    
    for (uint8_t cmd : cmds) {
        sendCommand(cmd);
    }
    
    // Prepare DMA buffer with data command prefix
    dma_buffer_[0] = 0x40; // Data command
    memcpy(dma_buffer_ + 1, buffer_, BUFFER_SIZE);
    
    // Configure DMA channel for I2C transfer
    dma_channel_config config = dma_channel_get_default_config(dma_chan_);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, i2c_get_dreq(i2c0, true)); // I2C TX DREQ
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    
    // Start DMA transfer (non-blocking)
    dma_channel_configure(
        dma_chan_,
        &config,
        &i2c0->hw->data_cmd, // Write to I2C data register
        dma_buffer_,
        BUFFER_SIZE + 1,
        true // Start immediately
    );
    
    display_busy_ = true;
    return true;
}

bool OledDisplay::isDisplayBusy() {
    if (!display_busy_) return false;
    
    // Check if DMA transfer is complete
    if (!dma_channel_is_busy(dma_chan_)) {
        display_busy_ = false;
    }
    
    return display_busy_;
}

void OledDisplay::writeText(const std::string& text, int16_t x, int16_t y) {
    if (!initialized_) return;
    
    int16_t current_x = x;
    int16_t current_y = y;
    
    for (char c : text) {
        if (c == '\n') {
            current_x = x;
            current_y += 8;
            continue;
        }
        
        if (current_x > SCREEN_WIDTH - 8) {
            current_x = x;
            current_y += 8;
        }
        
        if (current_y > SCREEN_HEIGHT - 8) break;
        
        writeChar(current_x, current_y, c);
        current_x += 8;
    }
}

void OledDisplay::setPixel(int x, int y, bool on) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    
    int byte_idx = (y / 8) * SCREEN_WIDTH + x;
    uint8_t bit_mask = 1 << (y % 8);
    
    if (on) {
        buffer_[byte_idx] |= bit_mask;
    } else {
        buffer_[byte_idx] &= ~bit_mask;
    }
}

void OledDisplay::drawLine(int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    
    while (true) {
        setPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void OledDisplay::invertDisplay(bool invert) {
    if (!initialized_) return;
    sendCommand(invert ? SSD1306_SET_INV_DISP : SSD1306_SET_NORM_DISP);
}

void OledDisplay::startScrolling() {
    if (!initialized_) return;
    
    uint8_t scroll_cmds[] = {
        SSD1306_SET_HORIZ_SCROLL | 0x00,  // right horizontal scroll
        0x00,                             // dummy byte
        0x00,                             // start page 0
        0x00,                             // time interval
        SSD1306_NUM_PAGES - 1,            // end page
        0x00,                             // dummy byte
        0xFF,                             // dummy byte
        SSD1306_SET_SCROLL | 0x01         // activate scroll
    };
    
    for (uint8_t cmd : scroll_cmds) {
        sendCommand(cmd);
    }
}

void OledDisplay::stopScrolling() {
    if (!initialized_) return;
    sendCommand(SSD1306_SET_SCROLL | 0x00);
}

void OledDisplay::setBrightness(uint8_t brightness) {
    if (!initialized_) return;
    sendCommand(SSD1306_SET_CONTRAST);
    sendCommand(brightness);
}

void OledDisplay::sleep(bool enable) {
    if (!initialized_) return;
    sendCommand(SSD1306_SET_DISP | (enable ? 0x00 : 0x01));
}

void OledDisplay::sendCommand(uint8_t cmd) {
    uint8_t buf[2] = {0x80, cmd};
    i2c_write_blocking(i2c0, i2c_addr_, buf, 2, false);
}

void OledDisplay::sendBuffer() {
    // Fallback to blocking transfer for compatibility
    uint8_t temp_buf[BUFFER_SIZE + 1];
    temp_buf[0] = 0x40;
    memcpy(temp_buf + 1, buffer_, BUFFER_SIZE);
    i2c_write_blocking(i2c0, i2c_addr_, temp_buf, BUFFER_SIZE + 1, false);
}

void OledDisplay::writeChar(int16_t x, int16_t y, uint8_t ch) {
    if (x > SCREEN_WIDTH - 8 || y > SCREEN_HEIGHT - 8) return;
    
    y = y / 8;
    ch = toupper(ch);
    
    int font_idx;
    if (ch >= 'A' && ch <= 'Z') {
        font_idx = ch - 'A' + 1;
    } else if (ch >= '0' && ch <= '9') {
        font_idx = ch - '0' + 27;
    } else {
        font_idx = 0;
    }
    
    int fb_idx = y * SCREEN_WIDTH + x;
    for (int i = 0; i < 8; i++) {
        buffer_[fb_idx++] = font[font_idx * 8 + i];
    }
}

static OledDisplay* global_oled = nullptr;

void writeToOled(const std::string& text, int16_t x, int16_t y) {
    if (!global_oled) {
        global_oled = new OledDisplay();
        global_oled->init();
    }
    global_oled->clear();
    global_oled->writeText(text, x, y);
    global_oled->display();
}

void clearOled() {
    if (global_oled) {
        global_oled->clear();
        global_oled->display();
    }
}

void invertOled(bool invert) {
    if (global_oled) {
        global_oled->invertDisplay(invert);
    }
}

void startScrollOled() {
    if (global_oled) {
        global_oled->startScrolling();
    }
}

void stopScrollOled() {
    if (global_oled) {
        global_oled->stopScrolling();
    }
}