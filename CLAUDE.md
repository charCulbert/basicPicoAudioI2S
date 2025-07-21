# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses CMake with the Raspberry Pi Pico SDK. Key commands:

**Build (Release):**
```bash
mkdir -p cmake-build-release-pico
cd cmake-build-release-pico
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2_w ..
make -j4
```

**Build (Debug):**
```bash
mkdir -p cmake-build-debug-pico
cd cmake-build-debug-pico
cmake -DCMAKE_BUILD_TYPE=Debug -DPICO_BOARD=pico2_w ..
make -j4
```

**Flash to Device:**
Copy the generated `.uf2` file to the Pico when in BOOTSEL mode, or use a debugger probe.

## Hardware Configuration

- **Target Board:** Raspberry Pi Pico 2 W (RP2350)
- **Audio Output:** I2S via PIO (pins 19-21)
- **System Clock:** 250MHz overclock (VREG_VOLTAGE_1_15)
- **Audio Format:** 44.1kHz, stereo, 128-frame buffers
- **Debug Pin:** GPIO 26 (for timing analysis)

## Code Architecture

### Dual-Core Design
- **Core 0:** Control thread (MIDI, rotary encoders, parameter management)
- **Core 1:** Real-time audio processing thread

### Audio Processing Pipeline
```
AudioEngine -> AudioModules -> I2sAudioOutput -> PIO/DMA -> Hardware
```

**Key Components:**
- `AudioEngine` - Module manager and processing coordinator
- `AudioModule` - Abstract base for all audio processing units
- `I2sAudioOutput` - Hardware driver using PIO state machine and DMA
- `ParameterStore` - Global parameter management system

### Fixed-Point Arithmetic
The project uses 16.15 signed fixed-point format (`fix15`) for audio processing:
- Range: ±65536 with ~0.00003 resolution
- Core operations in `Fix15.h`
- All audio modules process `fix15` samples via CHOC buffers

### Audio Modules
- `SimpleFixedOscModule` - Fixed-point sine wave oscillator with lookup table
- Modules implement `AudioModule::process(choc::buffer::InterleavedView<fix15>&)`
- Support for FM synthesis, envelopes, and reverb

### Hardware Abstraction
- `I2sAudioOutput` vs `PwmAudioOutput` - swappable via template/typedef
- PIO program in `audio_i2s.pio` handles I2S protocol timing
- Double-buffered DMA for glitch-free audio

## Dependencies

- **Pico SDK 2.1.1+** (minimum 2.0.0 required)
- **CHOC Library** - Audio buffer management and utilities (included in `choc/` directory)
- **C++17** standard required
- **PIO Assembly** for I2S implementation

## Development Notes

- System uses real-time constraints - avoid blocking operations in audio thread
- MIDI input via USB serial
- Parameter changes are lock-free via global parameter store
- Debug timing via GPIO 26 toggle in audio callback
- Fixed-point math preferred over floating-point for performance

## Common Patterns

**Adding New Audio Modules:**
1. Inherit from `AudioModule`
2. Implement `process(choc::buffer::InterleavedView<fix15>&)`
3. Add to engine in `main_core1()`
4. Use `fix15` arithmetic for all audio processing

**Parameter Management:**
- Add parameters in `ParameterStore.h::initialize_parameters()`
- Access via `g_synth_parameters` global vector
- Thread-safe reads from audio thread