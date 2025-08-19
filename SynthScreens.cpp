#include "SynthScreens.h"
#include "ParameterStore.h"
#include <cmath>
#include <algorithm>
#include <cctype>

SynthScreenManager::SynthScreenManager() 
    : current_screen_(SynthScreen::WAVEFORM)
    , last_auto_screen_(SynthScreen::PARAM_ONLY)
    , screen_switch_time_(0)
    , screen_timeout_ms_(500)  // Show parameter screen for 1 second
    , last_update_time_(0)
    , update_interval_ms_(100)  // 10fps for smooth graphics
    , has_pending_update_(false)
    , waveform_write_pos_(0)
    , waveform_scale_(1.0f)
    , display_(nullptr) {
    
    display_ = new OledDisplay();
    display_->init();
    
    // Initialize waveform buffer to zero
    for (int i = 0; i < WAVEFORM_BUFFER_SIZE; i++) {
        waveform_buffer_[i] = 0.0f;
    }
    
    // Load actual parameter values from the synth instead of using defaults
    loadParameterValuesFromStore();
}

void SynthScreenManager::showParameter(const std::string& name, float value) {
    // Store the parameter value
    storeParameterValue(name, value);
    
    // Always capture latest values for rate limiting
    pending_param_name_ = name;
    pending_param_value_ = value;
    has_pending_update_ = true;
    
    // Minimize screen switching overhead - only check occasionally
    static uint32_t last_screen_check = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if ((now - last_screen_check) < 50) {
        // Skip screen logic for rapid changes (< 50ms apart)
        return;
    }
    last_screen_check = now;
    
    // Detect which screen this parameter belongs to
    SynthScreen target_screen = detectScreenFromParameter(name);
    
    // Switch to the appropriate screen (only if different from current)
    if (target_screen != SynthScreen::PARAM_ONLY && target_screen != current_screen_) {
        switchToScreen(target_screen);
        // Force immediate update when switching screens
        has_pending_update_ = true;
    } else if (target_screen != SynthScreen::PARAM_ONLY) {
        // If already on correct screen, just refresh the timeout
        screen_switch_time_ = now;
    }
}

void SynthScreenManager::update() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Handle screen timeout - return to waveform view  
    if (current_screen_ != SynthScreen::WAVEFORM && 
        (now - screen_switch_time_) > screen_timeout_ms_) {
        current_screen_ = SynthScreen::WAVEFORM;
    }
    
    // Simple update logic: parameter screens when needed, waveform continuously
    if (has_pending_update_) {
        // Immediate update for parameter changes
        drawCurrentScreen();
        has_pending_update_ = false;
        last_update_time_ = now;
    } else if (current_screen_ == SynthScreen::WAVEFORM && 
               (now - last_update_time_) >= update_interval_ms_) {
        // Continuous updates for waveform screen only
        drawCurrentScreen();
        last_update_time_ = now;
    }
    
    // Also update parameter screens periodically when active (for fader animation)
    else if (current_screen_ != SynthScreen::WAVEFORM && 
             (now - last_update_time_) >= update_interval_ms_) {
        drawCurrentScreen();
        last_update_time_ = now;
    }
}

void SynthScreenManager::switchToScreen(SynthScreen screen) {
    current_screen_ = screen;
    screen_switch_time_ = to_ms_since_boot(get_absolute_time());
    last_auto_screen_ = screen;
}

void SynthScreenManager::nextScreen() {
    switch (current_screen_) {
        case SynthScreen::PARAM_ONLY: switchToScreen(SynthScreen::ADSR); break;
        case SynthScreen::ADSR: switchToScreen(SynthScreen::MIXER); break;
        case SynthScreen::MIXER: switchToScreen(SynthScreen::FILTER); break;
        case SynthScreen::FILTER: switchToScreen(SynthScreen::PWM); break;
        case SynthScreen::PWM: switchToScreen(SynthScreen::MASTER); break;
        case SynthScreen::MASTER: switchToScreen(SynthScreen::WAVEFORM); break;
        case SynthScreen::WAVEFORM: switchToScreen(SynthScreen::PARAM_ONLY); break;
    }
}

// Parameter type detection
SynthScreen SynthScreenManager::detectScreenFromParameter(const std::string& name) {
    if (isADSRParam(name)) return SynthScreen::ADSR;
    if (isMixerParam(name)) return SynthScreen::MIXER;
    if (isFilterParam(name)) return SynthScreen::FILTER;
    if (isPWMParam(name)) return SynthScreen::PWM;
    if (isMasterParam(name)) return SynthScreen::MASTER;
    return SynthScreen::PARAM_ONLY;
}

bool SynthScreenManager::isADSRParam(const std::string& name) {
    return name == "attack" ||
           name == "decay" ||
           name == "sustain" ||
           name == "release";
}

bool SynthScreenManager::isMixerParam(const std::string& name) {
    return name == "sawLevel" ||
           name == "pulseLevel" ||
           name == "subLevel" ||
           name == "noiseLevel";
}

bool SynthScreenManager::isFilterParam(const std::string& name) {
    return name == "filterCutoff" ||
           name == "filterResonance" ||
           name == "filterEnvAmount" ||
           name == "filterKeyboardTracking";
}


bool SynthScreenManager::isPWMParam(const std::string& name) {
    return name == "pulseWidth" ||
           name == "pwmLfoAmount" ||
           name == "pwmLfoRate" ||
           name == "pwmEnvAmount";
}

bool SynthScreenManager::isMasterParam(const std::string& name) {
    return name == "masterVol";
}

void SynthScreenManager::storeParameterValue(const std::string& name, float value) {
    // Use exact parameter names for reliable matching
    
    // ADSR parameters
    if (name == "attack") adsr_attack_ = value;
    else if (name == "decay") adsr_decay_ = value;
    else if (name == "sustain") adsr_sustain_ = value;
    else if (name == "release") adsr_release_ = value;
    
    // Mixer parameters
    else if (name == "sawLevel") mixer_saw_ = value;
    else if (name == "pulseLevel") mixer_pulse_ = value;
    else if (name == "subLevel") mixer_sub_ = value;
    else if (name == "noiseLevel") mixer_noise_ = value;
    else if (name == "masterVol") mixer_master_ = value;
    
    // Filter parameters  
    else if (name == "filterCutoff") filter_cutoff_ = value;
    else if (name == "filterResonance") filter_resonance_ = value;
    else if (name == "filterEnvAmount") filter_envelope_ = value;
    else if (name == "filterKeyboardTracking") filter_keyboard_ = value;
    
    // PWM parameters
    else if (name == "pulseWidth") pulse_width_ = value;
    else if (name == "pwmLfoAmount") pwm_lfo_amount_ = value;
    else if (name == "pwmLfoRate") pwm_lfo_rate_ = value;
    else if (name == "pwmEnvAmount") pwm_env_amount_ = value;
    
    // Waveform display parameters
    else if (name == "waveformToggle") waveform_scale_ = 1.0f + (value * 9.0f);  // Scale from 1x to 10x
}

void SynthScreenManager::drawCurrentScreen() {
    switch (current_screen_) {
        case SynthScreen::ADSR: drawADSRScreen(); break;
        case SynthScreen::MIXER: drawMixerScreen(); break;
        case SynthScreen::FILTER: drawFilterScreen(); break;
        case SynthScreen::PWM: drawPWMScreen(); break;
        case SynthScreen::MASTER: drawMasterScreen(); break;
        case SynthScreen::WAVEFORM: drawWaveformScreen(); break;
        case SynthScreen::PARAM_ONLY: drawParamOnlyScreen(); break;
    }
}

void SynthScreenManager::drawADSRScreen() {
    clearScreen();
    
    // Four ADSR faders - same as all other screens
    const int bar_width = 18;
    const int bar_height = 40;
    const int bar_y = 4;
    
    // Draw faders with centered labels
    drawFader(15, bar_y, bar_width, bar_height, adsr_attack_, "A");
    drawFader(40, bar_y, bar_width, bar_height, adsr_decay_, "D");
    drawFader(65, bar_y, bar_width, bar_height, adsr_sustain_, "S");
    drawFader(90, bar_y, bar_width, bar_height, adsr_release_, "R");
    
    updateDisplay();
}

void SynthScreenManager::drawMixerScreen() {
    clearScreen();
    
    // All faders same width - no title
    const int bar_width = 18;
    const int bar_height = 40;
    const int bar_y = 4;
    
    // Draw faders with centered labels - now only 4 oscillator levels
    drawFader(15, bar_y, bar_width, bar_height, mixer_saw_, "S");
    drawFader(40, bar_y, bar_width, bar_height, mixer_pulse_, "P");
    drawFader(65, bar_y, bar_width, bar_height, mixer_sub_, "SB");
    drawFader(90, bar_y, bar_width, bar_height, mixer_noise_, "NS");
    
    updateDisplay();
}

void SynthScreenManager::drawFilterScreen() {
    clearScreen();
    
    // Four faders - same width, no title, no percentages
    const int bar_width = 18;
    const int bar_height = 40;
    const int bar_y = 4;
    
    // Draw faders with centered labels
    drawFader(15, bar_y, bar_width, bar_height, filter_cutoff_, "CUT");
    drawFader(40, bar_y, bar_width, bar_height, filter_resonance_, "RES");
    drawFader(65, bar_y, bar_width, bar_height, filter_envelope_, "ENV");
    drawFader(90, bar_y, bar_width, bar_height, filter_keyboard_, "KBD");
    
    updateDisplay();
}

void SynthScreenManager::drawPWMScreen() {
    clearScreen();
    
    // Four PWM faders - same width
    const int bar_width = 18;
    const int bar_height = 40;
    const int bar_y = 4;
    
    // Draw faders with centered labels
    drawFader(15, bar_y, bar_width, bar_height, pulse_width_, "PW");
    drawFader(40, bar_y, bar_width, bar_height, pwm_lfo_amount_, "LFO");
    drawFader(65, bar_y, bar_width, bar_height, pwm_lfo_rate_, "RT");
    drawFader(90, bar_y, bar_width, bar_height, pwm_env_amount_, "ENV");
    
    updateDisplay();
}

void SynthScreenManager::drawMasterScreen() {
    clearScreen();
    
    // Single large master volume fader
    const int bar_width = 18;
    const int bar_height = 40;
    const int bar_x = 64 - bar_width/2; // Center horizontally
    const int bar_y = 4;
    
    // Draw centered master volume fader
    drawFader(bar_x, bar_y, bar_width, bar_height, mixer_master_, "MST");
    
    updateDisplay();
}

void SynthScreenManager::drawWaveformScreen() {
    clearScreen();
    
    // Draw oscilloscope-style waveform
    const int scope_x = 0;
    const int scope_y = 4;
    const int scope_w = 128;
    const int scope_h = 50;
    const int center_y = scope_y + scope_h / 2;
    
    // No center line - cleaner waveform display
    
    // Draw waveform samples starting from current write position for most recent data
    // Use interpolation to make waveform look smoother despite lower sample rate
    for (int x = 0; x < scope_w - 1; x++) {
        // Calculate buffer indices for circular buffer
        int idx1 = (waveform_write_pos_ - scope_w + x + WAVEFORM_BUFFER_SIZE) % WAVEFORM_BUFFER_SIZE;
        int idx2 = (waveform_write_pos_ - scope_w + x + 1 + WAVEFORM_BUFFER_SIZE) % WAVEFORM_BUFFER_SIZE;
        
        // Scale samples to display height using adjustable scale factor (oscilloscope-like scaling)
        int y1 = center_y - (int)(waveform_buffer_[idx1] * scope_h * waveform_scale_ * 2.0f);
        int y2 = center_y - (int)(waveform_buffer_[idx2] * scope_h * waveform_scale_ * 2.0f);
        
        // Clamp to screen bounds  
        y1 = std::max(scope_y, std::min(y1, scope_y + scope_h - 1));
        y2 = std::max(scope_y, std::min(y2, scope_y + scope_h - 1));
        
        // Draw line segment
        drawLine(scope_x + x, y1, scope_x + x + 1, y2);
    }
    
    // Add label at bottom
    drawText("WAVEFORM", 35, 58);
    
    updateDisplay();
}

void SynthScreenManager::feedAudioSamples(const float* samples, int num_samples) {
    // Simply copy samples into circular buffer
    for (int i = 0; i < num_samples; i++) {
        waveform_buffer_[waveform_write_pos_] = samples[i];
        waveform_write_pos_++;
        
        // Wrap around when buffer is full
        if (waveform_write_pos_ >= WAVEFORM_BUFFER_SIZE) {
            waveform_write_pos_ = 0;
        }
    }
}

void SynthScreenManager::drawParamOnlyScreen() {
    clearScreen();
    
    if (has_pending_update_ || !pending_param_name_.empty()) {
        // Simple parameter display
        std::string name = pending_param_name_.length() > 16 ? pending_param_name_.substr(0, 16) : pending_param_name_;
        drawText(name, 0, 20);
        drawText(std::to_string((int)(pending_param_value_ * 100)) + "%", 0, 35);
    } else {
        // Show default message when no parameter is active
        drawText("PICO SYNTH", 0, 20);
        drawText("Ready", 0, 35);
    }
    
    updateDisplay();
}

void SynthScreenManager::loadParameterValuesFromStore() {
    // Load all parameter values from the global parameter store
    for (auto* param : g_synth_parameters) {
        if (param) {
            storeParameterValue(param->getID(), param->getNormalizedValue());
        }
    }
}

// Drawing helpers
void SynthScreenManager::clearScreen() {
    display_->clear();
}

void SynthScreenManager::drawText(const std::string& text, int x, int y) {
    display_->writeText(text, x, y);
}

void SynthScreenManager::drawLine(int x0, int y0, int x1, int y1) {
    display_->drawLine(x0, y0, x1, y1);
}

void SynthScreenManager::drawRect(int x, int y, int w, int h, bool filled) {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            if (filled || i == 0 || i == w-1 || j == 0 || j == h-1) {
                display_->setPixel(x + i, y + j);
            }
        }
    }
}

void SynthScreenManager::drawBar(int x, int y, int w, int h, float level) {
    // Draw outline
    drawRect(x, y, w, h, false);
    
    // Fill vertically from bottom based on level
    int fill_h = (int)(level * (h - 2));
    if (fill_h > 0) {
        drawRect(x + 1, y + h - 1 - fill_h, w - 2, fill_h, true);
    }
}

// Helper to draw a labeled fader with proper alignment
void SynthScreenManager::drawFader(int x, int y, int w, int h, float level, const std::string& label) {
    drawBar(x, y, w, h, level);
    
    // Center the label under the fader
    int text_width = label.length() * 6; // Approximate character width
    int label_x = x + (w - text_width) / 2;
    int label_y = y + h + 4; // 1 pixel below fader
    
    drawText(label, label_x, label_y);
}

void SynthScreenManager::updateDisplay() {
    // Try non-blocking display update first, fallback to blocking if DMA unavailable
    if (!display_->displayAsync()) {
        display_->display(); // Fallback to blocking
    }
}

// Global interface
static SynthScreenManager* global_screen_manager = nullptr;

void showSynthParameter(const std::string& name, float value) {
    if (!global_screen_manager) {
        global_screen_manager = new SynthScreenManager();
    }
    global_screen_manager->showParameter(name, value);
}

void updateSynthScreens() {
    if (global_screen_manager) {
        global_screen_manager->update();
    }
}

void switchSynthScreen(SynthScreen screen) {
    if (global_screen_manager) {
        global_screen_manager->switchToScreen(screen);
    }
}

void nextSynthScreen() {
    if (global_screen_manager) {
        global_screen_manager->nextScreen();
    }
}

void feedSynthWaveform(const float* samples, int num_samples) {
    if (global_screen_manager) {
        global_screen_manager->feedAudioSamples(samples, num_samples);
    }
}
