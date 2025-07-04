# -----------------------------------------------------------------------------
# Minimum required CMake version
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico)

# -----------------------------------------------------------------------------
# Set environment variables for the Pico SDK and Pico Extras.
# These should point to your local copies.
# -----------------------------------------------------------------------------
set(ENV{PICO_SDK_PATH} "~/pico/pico-sdk")
set(ENV{PICO_EXTRAS_PATH} "~/pico/pico-extras")

# -----------------------------------------------------------------------------
# Tell the import scripts NOT to fetch from Git (use local copies instead)
# -----------------------------------------------------------------------------
set(PICO_SDK_FETCH_FROM_GIT 0)
set(PICO_EXTRAS_FETCH_FROM_GIT 0)

# -----------------------------------------------------------------------------
# Optional: Specify exact Git tags for reproducible builds
# ---------------------------------------------e--------------------------------
# set(PICO_SDK_GIT_TAG v1.5.0)
# set(PICO_EXTRAS_GIT_TAG v1.5.0)

# -----------------------------------------------------------------------------
# Include the Pico SDK and Pico Extras import scripts.
# These must be included before calling project().
# -----------------------------------------------------------------------------
include(pico_sdk_import.cmake)
# include(pico_extras_import.cmake)

# -----------------------------------------------------------------------------
# Define the project and the languages used (C, C++, and ASM)
# -----------------------------------------------------------------------------
project(basicPicoAudio C CXX ASM)

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
# Define the executable target 'basicPicoAudio' with its source files.
# -----------------------------------------------------------------------------
add_executable(basicPicoAudio
        main.cpp
)

pico_generate_pio_header(basicPicoAudio ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)


# -----------------------------------------------------------------------------
# Specify include directories for the target
# -----------------------------------------------------------------------------
target_include_directories(basicPicoAudio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}

        choc

)

# -----------------------------------------------------------------------------
# Link the target with the required Pico libraries
# -----------------------------------------------------------------------------
target_link_libraries(basicPicoAudio PRIVATE
        pico_stdlib
        pico_multicore
        hardware_dma
        hardware_pwm
        hardware_pio
        tinyusb_device
)

# -----------------------------------------------------------------------------
# Set C++ compile features (C++17 in this case)
# -----------------------------------------------------------------------------
target_compile_features(basicPicoAudio PRIVATE cxx_std_17)

# -----------------------------------------------------------------------------
# Define compile-time preprocessor definitions for I2S configuration.
# Adjust these definitions as needed.
# -----------------------------------------------------------------------------
target_compile_definitions(basicPicoAudio PRIVATE
)

# -------------------------------------------------------------------------------
# Enable USB serial and UART for debugging
# -----------------------------------------------------------------------------
pico_enable_stdio_usb(basicPicoAudio 1)
pico_enable_stdio_uart(basicPicoAudio 1)

# -----------------------------------------------------------------------------
# Generate additional output files (map/bin/hex, etc.)
# -----------------------------------------------------------------------------
pico_add_extra_outputs(basicPicoAudio)
