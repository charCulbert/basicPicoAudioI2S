# Pico Synth 
Synthesizer for rp2040 or rp2350. Core 0 handles outside communication (USB Serial MIDI, OLED display), and Core 1 handles all audio processing. I am using an I2S DAC, the PCM5102A as the output stage. 

As much of the synthesis is done in a Fixed Point 16.15 format as possible to avoid float operations, which are slow especially on the rp2040 which has no FPU like the 2350 does. More operations could still be converted to Fix rather than float, but it's a good start.

The synth's architecture is modeled roughly after a Roland SH101. 

Also included is an html page which serves as a controller for the synth -- auto-populating the necessary knobs for midi cc controls. You can pass MIDI through an IAC driver, or use the html keyboard / sequencer.

## Hardware Requirements
- Raspberry Pi Pico 2 W (RP2350) or Pico (RP2040)
- I2S DAC connected to pins 19-21
- Optional: MIDI input via USB serial
- An I2C OLED display, I am using SSD1306 with pins 4/5 as SDA/SCL. 


<img src="picoSetup.png" width="250"> 

*The pico connected to the PCM5102A -- seen here setup with a second pico acting as a debugger.*
![HTML Controller](htmlController.png)
*HTML controller will automatically load params from the pico upon connection*


