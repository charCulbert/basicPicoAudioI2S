# -----------------------------------------------------------------------------
# Minimum required CMake version
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico2)

# -----------------------------------------------------------------------------
# Set environment variables for the Pico SDK and Pico Extras.
# These should point to your local copies.
# -----------------------------------------------------------------------------
set(ENV{PICO_SDK_PATH} "/home/char/pico/pico-sdk")
set(ENV{PICO_EXTRAS_PATH} "/home/char/pico/pico-extras")

# -----------------------------------------------------------------------------
# Tell the import scripts NOT to fetch from Git (use local copies instead)
# -----------------------------------------------------------------------------
set(PICO_SDK_FETCH_FROM_GIT 0)
set(PICO_EXTRAS_FETCH_FROM_GIT 0)

# -----------------------------------------------------------------------------
# Optional: Specify exact Git tags for reproducible builds
# -----------------------------------------------------------------------------
# set(PICO_SDK_GIT_TAG v1.5.0)
# set(PICO_EXTRAS_GIT_TAG v1.5.0)

# -----------------------------------------------------------------------------
# Include the Pico SDK and Pico Extras import scripts.
# These must be included before calling project().
# -----------------------------------------------------------------------------
include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

# -----------------------------------------------------------------------------
# Define the project and the languages used (C, C++, and ASM)
# -----------------------------------------------------------------------------
project(basicPicoAudioI2S C CXX ASM)

# -----------------------------------------------------------------------------
# Set the C and C++ standard versions
# -----------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------------
# Initialize the Pico SDK (this sets up necessary SDK variables and targets)
# -----------------------------------------------------------------------------
pico_sdk_init()

# -----------------------------------------------------------------------------
# Verify that the Pico SDK version is at least 2.0.0.
# -----------------------------------------------------------------------------
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Require at least Raspberry Pi Pico SDK version 2.0.0")
endif()

# -----------------------------------------------------------------------------
# Define the executable target 'sine_wave_i2s' with its source files.
# -----------------------------------------------------------------------------
add_executable(sine_wave_i2s
        main.cpp
        chocSineModule.cpp
        chocSineModule.h
        AudioModule.h
        AudioEngine.cpp
        AudioEngine.h
        freqModSineModule.cpp
        freqModSineModule.h
        ParameterPassing.h
)

# -----------------------------------------------------------------------------
# Specify include directories for the target
# -----------------------------------------------------------------------------
target_include_directories(sine_wave_i2s PRIVATE include)

# -----------------------------------------------------------------------------
# Link the target with the required Pico libraries
# -----------------------------------------------------------------------------
target_link_libraries(sine_wave_i2s PRIVATE
        pico_stdlib
        pico_multicore
        pico_audio_i2s
)

# -----------------------------------------------------------------------------
# Set C++ compile features (C++17 in this case)
# -----------------------------------------------------------------------------
target_compile_features(sine_wave_i2s PRIVATE cxx_std_17)

# -----------------------------------------------------------------------------
# Define compile-time preprocessor definitions for I2S configuration.
# Adjust these definitions as needed.
# -----------------------------------------------------------------------------
target_compile_definitions(sine_wave_i2s PRIVATE
        USE_AUDIO_I2S=1
        PICO_AUDIO_I2S_DATA_PIN=10
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=11
        # Optional:
        # PICO_AUDIO_I2S_MONO_INPUT=1
        # PICO_AUDIO_I2S_MONO_OUTPUT=1
        # PICO_DEFAULT_UART=0
        # PICO_DEFAULT_UART_TX_PIN=28
        # PICO_DEFAULT_UART_RX_PIN=29
)

# -----------------------------------------------------------------------------
# Enable USB serial and UART for debugging
# -----------------------------------------------------------------------------
pico_enable_stdio_usb(sine_wave_i2s 1)
pico_enable_stdio_uart(sine_wave_i2s 1)

# -----------------------------------------------------------------------------
# Generate additional output files (map/bin/hex, etc.)
# -----------------------------------------------------------------------------
pico_add_extra_outputs(sine_wave_i2s)
