#pragma once

#include "pico/stdlib.h"
#include "OledDisplay.h"
#include <string>

enum class SynthScreen {
    ADSR,
    MIXER,
    FILTER,
    PWM,  // Pulse Width Modulation screen with 4 faders
    MASTER,  // Master volume screen
    WAVEFORM,  // Oscilloscope view of current audio
    PARAM_ONLY  // Just show parameter name/value
};

class SynthScreenManager {
public:
    SynthScreenManager();
    
    // Main interface - shows parameter and switches screens intelligently
    void showParameter(const std::string& name, float value);
    void update();
    
    // Audio data for oscilloscope
    void feedAudioSamples(const float* samples, int num_samples);
    
    // Manual screen switching
    void switchToScreen(SynthScreen screen);
    void nextScreen();
    
    // Screen timeout settings
    void setScreenTimeout(uint32_t timeout_ms) { screen_timeout_ms_ = timeout_ms; }
    void setUpdateRate(uint32_t ms) { update_interval_ms_ = ms; }
    
private:
    SynthScreen current_screen_;
    SynthScreen last_auto_screen_;
    uint32_t screen_switch_time_;
    uint32_t screen_timeout_ms_;
    uint32_t last_update_time_;
    uint32_t update_interval_ms_;
    
    std::string pending_param_name_;
    float pending_param_value_;
    bool has_pending_update_;
    
    // Parameter values for different screens
    float adsr_attack_ = 0.1f;
    float adsr_decay_ = 0.3f;
    float adsr_sustain_ = 0.7f;
    float adsr_release_ = 0.5f;
    
    float mixer_saw_ = 0.8f;
    float mixer_pulse_ = 0.6f;
    float mixer_sub_ = 0.4f;
    float mixer_noise_ = 0.1f;
    float mixer_master_ = 0.75f;
    
    float filter_cutoff_ = 0.6f;
    float filter_resonance_ = 0.3f;
    float filter_envelope_ = 0.4f;
    float filter_keyboard_ = 0.3f;
    
    float pulse_width_ = 0.5f;
    float pwm_lfo_amount_ = 0.1f;
    float pwm_lfo_rate_ = 0.5f;
    float pwm_env_amount_ = 0.2f;
    
    // Waveform display buffer for oscilloscope
    static const int WAVEFORM_BUFFER_SIZE = 128;  // 128 pixels wide
    float waveform_buffer_[WAVEFORM_BUFFER_SIZE];
    int waveform_write_pos_ = 0;
    float waveform_scale_ = 1.0f;  // Scaling factor for waveform display
    
    OledDisplay* display_;
    
    // Screen detection
    SynthScreen detectScreenFromParameter(const std::string& name);
    bool isADSRParam(const std::string& name);
    bool isMixerParam(const std::string& name);
    bool isFilterParam(const std::string& name);
    bool isPWMParam(const std::string& name);
    bool isMasterParam(const std::string& name);
    
    // Screen drawing methods
    void drawCurrentScreen();
    void drawADSRScreen();
    void drawMixerScreen();
    void drawFilterScreen();
    void drawPWMScreen();
    void drawMasterScreen();
    void drawWaveformScreen();
    void drawParamOnlyScreen();
    
    // Drawing helpers
    void clearScreen();
    void drawText(const std::string& text, int x, int y);
    void drawLine(int x0, int y0, int x1, int y1);
    void drawRect(int x, int y, int w, int h, bool filled = false);
    void drawBar(int x, int y, int w, int h, float level);
    void drawFader(int x, int y, int w, int h, float level, const std::string& label);
    void updateDisplay();
    
    // Store parameter values
    void storeParameterValue(const std::string& name, float value);
    void loadParameterValuesFromStore();
};

// Global interface functions
void showSynthParameter(const std::string& name, float value);
void updateSynthScreens();
void switchSynthScreen(SynthScreen screen);
void nextSynthScreen();
void feedSynthWaveform(const float* samples, int num_samples);