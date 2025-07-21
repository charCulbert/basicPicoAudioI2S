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

- **Target Board:** Raspberry Pi Pico 2 W (RP2350) or RP2040 (no FPU)
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
- Audio thread optimized for RP2040 (no FPU) with minimal float operations

### Audio Modules
- `SimpleFixedOscModule` - Complete monophonic synthesizer voice with sine oscillator
- `Fix15VCAEnvelopeModule` - High-performance ADSR envelope with classic analog behavior  
- `FilterModule` - Moog-style 4-pole ladder filter with resonance
- Modules implement `AudioModule::process(choc::buffer::InterleavedView<fix15>&)`
- Support for FM synthesis, envelopes, filtering, and reverb

### Filter System
The Moog ladder filter (`FilterModule`) features:
- **4-pole cascade** - Classic Moog topology with 24dB/octave rolloff
- **Resonance feedback** - From final stage back to input for characteristic sound
- **Per-sample responsiveness** - Immediate knob response for expressive playing
- **Stability clamping** - Prevents oscillation blowup while preserving character
- **Makeup gain** - 2.5x compensation for natural filter volume loss
- **Pure fix15 processing** - 6 multiplications per sample (4 poles + resonance + gain)

### Envelope System
The ADSR envelope (`Fix15VCAEnvelopeModule`) features:
- **Real-time parameter updates** - Classic analog synth behavior (parameters affect currently playing notes)
- **Click-free smoothing** - 10ms parameter transitions prevent audio artifacts
- **32-bit sample counting** - Handles envelope times up to hours without overflow
- **RP2040 optimized** - Peak 8 float operations per sample, rest is integer math
- **Professional envelope curves** - Linear attack/decay/release with smooth sustain tracking

### Hardware Abstraction
- `I2sAudioOutput` vs `PwmAudioOutput` - swappable via template/typedef
- PIO program in `audio_i2s.pio` handles I2S protocol timing
- Double-buffered DMA for glitch-free audio

## Dependencies

- **Pico SDK 2.1.1+** (minimum 2.0.0 required)
- **CHOC Library** - Audio buffer management and utilities (included in `choc/` directory)
- **C++17** standard required
- **PIO Assembly** for I2S implementation

## Performance Characteristics

- **Audio Thread CPU Usage:** Minimal (designed for 250MHz RP2350 or 133MHz RP2040)
- **Float Operations:** Peak 8 per sample during envelope decay phase
- **Memory Usage:** Static allocation, no heap usage in audio thread
- **Real-time Safety:** No blocking operations, no dynamic memory allocation
- **Latency:** ~3ms at 44.1kHz (128 sample buffer)

## Development Notes

- System uses real-time constraints - avoid blocking operations in audio thread
- MIDI input via USB serial
- Parameter changes are lock-free via global parameter store  
- Debug timing via GPIO 26 toggle in audio callback
- Fixed-point math preferred over floating-point for performance
- Envelope parameters respond immediately (classic analog synth behavior)

## Common Patterns

**Adding New Audio Modules:**
1. Inherit from `AudioModule`
2. Implement `process(choc::buffer::InterleavedView<fix15>&)`
3. Add to engine in `main_core1()`
4. Use `fix15` arithmetic for all audio processing
5. Minimize float operations in audio thread

**Parameter Management:**
- Add parameters in `ParameterStore.h::initialize_parameters()`
- Access via `g_synth_parameters` global vector
- Thread-safe reads from audio thread
- Parameters update immediately for expressive real-time control

**Envelope Integration:**
- Use `Fix15VCAEnvelopeModule` for ADSR envelopes
- Call `noteOn()` and `noteOff()` for gate control
- Use `getNextValue()` for envelope level or `process()` for VCA mode
- Parameters can be changed during playback for performance expression

**Filter Integration:**
- Use `FilterModule` for Moog-style filtering
- Add `filterCutoff` and `filterResonance` parameters to ParameterStore
- Filter processes entire audio buffer with per-sample coefficient updates
- Cutoff: 0-1 maps to ~20Hz-18kHz with musical exponential curve
- Resonance: 0-1 maps to no feedback up to near self-oscillation