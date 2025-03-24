cmake_minimum_required(VERSION 3.12)

# Tell the import scripts to fetch from Git if not found locally
set(PICO_SDK_FETCH_FROM_GIT 0)
set(PICO_EXTRAS_FETCH_FROM_GIT 0)

# (Optional) specify exact versions if you want
# set(PICO_SDK_GIT_TAG v1.5.0)
# set(PICO_EXTRAS_GIT_TAG v1.5.0)


# Pull in PICO SDK and PICO EXTRAS(needed for i2s audio)
# This MUST happen before creaitng the project
include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(simplePicoAudioI2S C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Require at least Raspberry Pi Pico SDK version 2.0.0")
endif()


# only build I2S example if library is available
    add_executable(sine_wave_i2s
            main.cpp
            chocSineModule.cpp
            chocSineModule.h
            AudioModule.h
            AudioEngine.cpp
            AudioEngine.h
            freqModSineModule.cpp
            freqModSineModule.h
    )

    target_include_directories(sine_wave_i2s PRIVATE
    include
    )
    target_link_libraries(sine_wave_i2s PRIVATE
            pico_stdlib
            pico_audio_i2s
            )
target_compile_features(sine_wave_i2s PRIVATE cxx_std_17)

    target_compile_definitions(sine_wave_i2s PRIVATE
            # compile time configuration of I2S
            USE_AUDIO_I2S=1
            PICO_AUDIO_I2S_DATA_PIN=10
            PICO_AUDIO_I2S_CLOCK_PIN_BASE=11
#            PICO_AUDIO_I2S_MONO_INPUT=1
#            PICO_AUDIO_I2S_MONO_OUTPUT=1
#            PICO_DEFAULT_UART=0
#            PICO_DEFAULT_UART_TX_PIN=28
#            PICO_DEFAULT_UART_RX_PIN=29
            )

    # enable usb serial
    pico_enable_stdio_usb(sine_wave_i2s 1)
    pico_enable_stdio_uart(sine_wave_i2s 1)
    # create map/bin/hex file etc.
    pico_add_extra_outputs(sine_wave_i2s)
