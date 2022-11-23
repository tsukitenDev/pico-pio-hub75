cmake_minimum_required(VERSION 3.16)

include(pico_sdk_import.cmake)

project(hub75 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_SDK_PATH "/home/tsuki/pico/pico-sdk")
pico_sdk_init()


add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )


add_executable(pico_hub75)

pico_generate_pio_header(pico_hub75 ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

target_sources(pico_hub75 PRIVATE main.cpp hub75.cpp)

target_compile_definitions(pico_hub75 PRIVATE
	PICO_DEFAULT_UART_TX_PIN=28
	PICO_DEFAULT_UART_RX_PIN=29
)

target_link_libraries(pico_hub75 PRIVATE pico_stdlib hardware_pio hardware_dma hardware_clocks pico_multicore)

# enable usb output, disable uart output
pico_enable_stdio_usb(pico_hub75 1)
pico_enable_stdio_uart(pico_hub75 0)


pico_add_extra_outputs(pico_hub75)

