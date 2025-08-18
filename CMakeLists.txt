cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(PicoSynth C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(PicoSynth
        main.cpp
)

pico_generate_pio_header(PicoSynth ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)

target_include_directories(PicoSynth PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        choc
)

# Fixed library linking - removed tinyusb_device which conflicts with pico_stdio_usb
target_link_libraries(PicoSynth PRIVATE
        pico_stdlib
        pico_multicore
        hardware_dma
        hardware_pwm
        hardware_pio
        hardware_interp
)

target_compile_features(PicoSynth PRIVATE cxx_std_17)

pico_enable_stdio_usb(PicoSynth 1)
pico_enable_stdio_uart(PicoSynth 0)

pico_add_extra_outputs(PicoSynth)
