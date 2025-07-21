# Basic Pico Audio I2S

Real-time audio synthesizer for Raspberry Pi Pico using I2S output and fixed-point arithmetic.

## Features

- **Pure fixed-point audio** - No floating-point in audio thread for RP2040 compatibility
- **Dual-core architecture** - Core 0 handles control, Core 1 processes audio
- **Moog ladder filter** - 4-pole resonant lowpass with per-sample responsiveness  
- **ADSR envelopes** - Click-free with real-time parameter updates
- **I2S audio output** - Digital audio via PIO state machine
- **Low latency** - 3ms at 44.1kHz with 128-sample buffers

## Hardware Requirements

- Raspberry Pi Pico 2 W (RP2350) or Pico (RP2040)
- I2S DAC connected to pins 19-21
- Optional: MIDI input via USB serial

## Quick Start

```bash
mkdir -p cmake-build-release-pico
cd cmake-build-release-pico
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2_w ..
make -j4
```

Copy generated `.uf2` file to Pico in BOOTSEL mode.

## Architecture

**Audio Pipeline:**
```
AudioEngine -> AudioModules -> I2sAudioOutput -> Hardware
```

**Key Modules:**
- `SimpleFixedOscModule` - Sine wave oscillator
- `Fix15VCAEnvelopeModule` - ADSR envelope generator
- `FilterModule` - Moog-style resonant filter

**Fixed-Point Format:**
- 16.15 signed (`fix15`) - Range ±65536, resolution ~0.00003
- All audio processing in fixed-point for performance
- Core operations in `Fix15.h`

## Performance

- **CPU Usage:** Optimized for 250MHz RP2350 / 133MHz RP2040
- **Filter:** 6 multiplications per sample (4 poles + resonance + gain)
- **Envelope:** Peak 8 float ops per sample, rest integer
- **Memory:** Static allocation, no heap usage in audio thread

## Dependencies

- Pico SDK 2.1.1+
- CHOC Library (included)
- C++17

Built with real-time constraints - no blocking operations or dynamic allocation in audio thread.