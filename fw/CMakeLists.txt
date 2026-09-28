cmake_minimum_required(VERSION 3.13)

# O SDK tem de ser encontrado antes de project().
if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Defina PICO_SDK_PATH (ambiente ou -D)")
    endif()
endif()

# RP2350B em QFN-80. O Pico 2 (RP2350A) tem 30 GPIO e nao chega:
# precisamos de ~40 (doc 10).
# Tem de estar definido ANTES do include do SDK, porque e' o include
# que carrega a plataforma e sao as flags dela que põem
# -mthumb -march=armv8-m.main.
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")

# Toolchain do SDK para Cortex-M33. E' ela que mete -mthumb e
# -march=armv8-m.main; sem isso o newlib usa cpsid/msr em modo ARM e o
# gcc falha com "selected processor does not support requested special
# purpose register". Define tambem CMAKE_TRY_COMPILE_TARGET_TYPE, sem
# o qual o teste de compilador do CMake nao consegue fazer link com o
# newlib de bare-metal.
set(CMAKE_TOOLCHAIN_FILE
    "${PICO_SDK_PATH}/cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake"
    CACHE FILEPATH "")
set(PICO_GCC_TRIPLE "arm-none-eabi" CACHE STRING "")

project(dreamcast_gdrom C CXX ASM)

# O SDK define CMAKE_EXECUTABLE_SUFFIX em PARENT_SCOPE dentro de
# pico_sdk_init(), mas o target ja' foi criado quando isso acontece.
# Sem o ..elf o picotool recusa-se a processar o binario.
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# O SDK 2.x mudou a API: inclui-se pico_sdk_init.cmake e chama-se
# pico_sdk_init() DEPOIS de project(). A versao antiga
# (include(pico_sdk_import)) ja nao existe. (O bloco PICO_SDK_PATH
# acima ja' tratou do caminho; nao o repetir aqui.)

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
pico_sdk_init()

include_directories(src)

add_executable(dreamcast_gdrom
    src/fw_main.c
    src/sniffer.c
)

# pico_generate_pio_header precisa do target ja' criado.
file(GLOB PIO_SOURCES ${CMAKE_CURRENT_LIST_DIR}/pio/*.pio)
pico_generate_pio_header(dreamcast_gdrom ${PIO_SOURCES})

target_link_libraries(dreamcast_gdrom
    pico_stdlib
    hardware_pio
    hardware_irq
    hardware_timer
    hardware_clocks
    pico_multicore
)

# O picotool exige extensao no nome do ELF/UF2, e o target por omissao
# nao a tem.
set_target_properties(dreamcast_gdrom PROPERTIES
    PICOTOOL_ELF_NAME "dreamcast_gdrom.elf"
    PICOTOOL_UF2_NAME "dreamcast_gdrom.uf2"
    PICOTOOL_BIN_NAME "dreamcast_gdrom.bin")

pico_add_extra_outputs(dreamcast_gdrom)
