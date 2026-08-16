# pico-infonesPlus
# IMPORTANT: Use the latest version of tinyusb and pico pio USB.

cmake_minimum_required(VERSION 3.13)

set(projectname piconesPlus)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Custom build type: RELEASENODEBUG
# usage: cmake -DCMAKE_BUILD_TYPE=RELEASENODEBUG ..
# ---------------------------------------------------------------------------
message("* Adding build types...")

set(GCC_RELEASENODEBUG_FLAGS "-O2 -DNDEBUG")

set(CMAKE_ASM_FLAGS_RELEASENODEBUG "${GCC_RELEASENODEBUG_FLAGS}"
    CACHE STRING "Flags used by the ASM compiler during REL_O2 builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASENODEBUG "${GCC_RELEASENODEBUG_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during REL_O2 builds." FORCE)
set(CMAKE_C_FLAGS_RELEASENODEBUG   "${GCC_RELEASENODEBUG_FLAGS}"
    CACHE STRING "Flags used by the C compiler during REL_O2 builds." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASENODEBUG ""
    CACHE STRING "Flags used for linking binaries during REL_O2 builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASENODEBUG ""
    CACHE STRING "Flags used by the shared libraries linker during REL_O2 builds." FORCE)

mark_as_advanced(
    CMAKE_CXX_FLAGS_RELEASENODEBUG
    CMAKE_ASM_FLAGS_RELEASENODEBUG
    CMAKE_C_FLAGS_RELEASENODEBUG
    CMAKE_EXE_LINKER_FLAGS_RELEASENODEBUG
    CMAKE_SHARED_LINKER_FLAGS_RELEASENODEBUG)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Choose the type of build : None Debug Release RelWithDebInfo MinSizeRel REL_O2."
        FORCE)
endif()
message("* Current build type is : ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Hardware / board selection (project-specific)
# ---------------------------------------------------------------------------
if(NOT HW_CONFIG)
    set(HW_CONFIG 1 CACHE STRING "Select the hardware configuration for your board")
endif()

if(NOT USE_HSTX)
    set(USE_HSTX 1 CACHE STRING "Use HSTX for display output when possible")
endif()

include("pico_shared/BoardConfigs.cmake")

# ---------------------------------------------------------------------------
# NES Zapper (light gun) on NES controller port 2.
#
# Custom PCB design v2.1 and later route port 2 pin D3 -> GPIO27 (light sensor)
# and pin D4 -> GPIO28 (trigger). Beware the v2.1 silkscreen, which labels these
# two pads the wrong way round; the routing is the same on v2.1 and v2.6 and
# needs no firmware difference. That is HW_CONFIG 2 (Adafruit DVI + SD breakout
# / custom PCB), the only supported configuration. Every other HW_CONFIG gets -1 ("not present"), which compiles
# the feature out entirely -- on most of them GPIO 27/28 are already taken by
# I2S clock pins, NES port 2, DVI/TMDS pairs or the PIO USB DP pin.
#
# Kept here rather than in pico_shared/BoardConfigs.cmake because that file is a
# submodule shared with the other emulators, which have no Zapper support.
#
# Note: this must come after BoardConfigs.cmake, which is where USE_I2S_AUDIO is
# set. Its value is either the string "0" or a driver name, hence STREQUAL.
# ---------------------------------------------------------------------------
# These are plain variables guarded by `if(NOT DEFINED ...)` rather than cache
# entries, so the board-dependent defaults are recomputed on every configure. A
# cache default would be baked into existing build directories and would not
# follow a change made here. An explicit -DZAPPER_...=x on the command line does
# create a cache entry, so DEFINED sees it and the override still wins.
if(NOT DEFINED ZAPPER_D3 AND NOT DEFINED ZAPPER_D4)
    if(HW_CONFIG EQUAL 2 AND "${USE_I2S_AUDIO}" STREQUAL "0")
        set(ZAPPER_D3 27) # light sensor
        set(ZAPPER_D4 28) # trigger
    else()
        # I2S audio on HW_CONFIG 2 claims GPIO 26 (data) and 27/28 (clock base
        # pair), so the Zapper pins have to give way if it is ever enabled there.
        set(ZAPPER_D3 -1)
        set(ZAPPER_D4 -1)
    endif()
endif()

# Line polarity, see zapper.h. The light line arrives inverted with respect to
# the value the NES puts on the bus: measured with a Tomee Zapp Gun, the light
# pin reads low while the sensor is covered and high while it sees light,
# whereas $4017 bit 3 is defined as 0 = light detected. The NES inverts this line
# between the connector and the CPU; a direct GPIO wire does not, so it has to be
# inverted in software. The trigger line needs no such correction.
# Override on the command line if a gun behaves differently, for example:
#   cmake -DHW_CONFIG=2 -DZAPPER_INVERT_D3=0 -DZAPPER_DEBUG=1 ..
if(NOT DEFINED ZAPPER_INVERT_D3)
    set(ZAPPER_INVERT_D3 1)
endif()
if(NOT DEFINED ZAPPER_INVERT_D4)
    set(ZAPPER_INVERT_D4 0)
endif()
if(NOT DEFINED ZAPPER_DEBUG)
    set(ZAPPER_DEBUG 0) # bit mask: 1 = UART trace, 2 = on-screen readout
endif()
# Display-lag measurement, see zapper.h. Replaces the picture with a black field
# and periodic white flashes, so it is a measuring instrument, not a play mode.
if(NOT DEFINED ZAPPER_MEASURE)
    set(ZAPPER_MEASURE 0)
endif()
if(NOT DEFINED ZAPPER_MEASURE_WHITE)
    set(ZAPPER_MEASURE_WHITE 1) # frames of white per flash
endif()
if(ZAPPER_D3 EQUAL -1 OR ZAPPER_D4 EQUAL -1)
    message("* Zapper support: disabled for HW_CONFIG ${HW_CONFIG}")
else()
    message("* Zapper support: D3 (light) = GPIO${ZAPPER_D3}, D4 (trigger) = GPIO${ZAPPER_D4}, "
            "invert D3/D4 = ${ZAPPER_INVERT_D3}/${ZAPPER_INVERT_D4}, debug = ${ZAPPER_DEBUG}, "
            "measure = ${ZAPPER_MEASURE}")
    if(ZAPPER_MEASURE)
        message("*   NOTE: ZAPPER_MEASURE replaces the picture with a black/white flash pattern.")
    endif()
endif()

if(NOT PICO_BOARD)
    set(PICO_BOARD pico CACHE STRING "Board type")
    message("PICO_BOARD not set, using default: ${PICO_BOARD}")
endif()

add_definitions(-DNDEBUG)

# ---------------------------------------------------------------------------
# TinyUSB family support + Pico SDK init
# Include family_support.cmake BEFORE pico_sdk_init() so that TinyUSB picks up
# the board-specific configuration (PIO USB VBUSEN pin, DP pin, etc.) from the
# BOARD variable set by BoardConfigs.cmake.  The Pico SDK's own TinyUSB
# integration forcibly sets BOARD="pico_sdk" which loses these board-specific
# defines; including family_support.cmake first (with its include_guard) prevents
# the SDK from overriding the board configuration.
# ---------------------------------------------------------------------------
set(FAMILY rp2040)
include($ENV{PICO_SDK_PATH}/lib/tinyusb/hw/bsp/family_support.cmake)

# Some boards (e.g. adafruit_metro_rp2350) force CFG_TUH_RPI_PIO_USB=1 in their
# board.cmake, which TinyUSB's rp2040/family.cmake then bakes into
# tinyusb_common_base. When PIO USB is disabled for this build, strip that
# interface define so it doesn't fight our -DCFG_TUH_RPI_PIO_USB=0 and drag
# hcd_pio_usb.c / pio_usb.h into the compile.
if(ENABLE_PIO_USB EQUAL 0 AND TARGET tinyusb_common_base)
    get_target_property(_tu_defs tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS)
    if(_tu_defs)
        list(REMOVE_ITEM _tu_defs "CFG_TUH_RPI_PIO_USB=1")
        set_target_properties(tinyusb_common_base PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "${_tu_defs}")
    endif()
endif()

project(${projectname} C CXX ASM)

pico_sdk_init()

# check_and_add_pico_pio_usb_support() needs pico_generate_pio_header which is
# only available after pico_sdk_init().  The first call inside family.cmake
# (before SDK init) was a no-op; this second call actually creates the
# tinyusb_pico_pio_usb target AND appends hcd_pio_usb.c / dcd_pio_usb.c to
# tinyusb_host_base / tinyusb_device_base. Skip it when PIO USB is disabled,
# otherwise those files get compiled into the project and fail looking for
# pio_usb.h (its include dir lives on tinyusb_pico_pio_usb, which we don't
# link in that case).
if(ENABLE_PIO_USB EQUAL 1)
    message("Checking for PIO USB support...")
    check_and_add_pico_pio_usb_support()
endif()
message("PIO USB support: ${ENABLE_PIO_USB}, using PIO: ${PIO_USB_USE_PIO}")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(${projectname}
    main.cpp
    splash.cpp
    DefaultSS444.c
    DefaultSS555.c
    state.cpp
    zapper.cpp
)

# Optional: embed a NES ROM directly into the firmware.
# Usage: cmake -DEMBED_NES_ROM=/path/to/rom.nes ..
if(EMBED_NES_ROM)
    if(NOT EXISTS "${EMBED_NES_ROM}")
        message(FATAL_ERROR "EMBED_NES_ROM file not found: ${EMBED_NES_ROM}")
    endif()
    set(EMBEDDED_ROM_C "${CMAKE_CURRENT_BINARY_DIR}/embedded_rom.c")
    add_custom_command(
        OUTPUT "${EMBEDDED_ROM_C}"
        COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=${EMBED_NES_ROM} -DOUTPUT_FILE=${EMBEDDED_ROM_C}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_embedded_rom.cmake
        DEPENDS "${EMBED_NES_ROM}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_embedded_rom.cmake"
        COMMENT "Embedding ROM: ${EMBED_NES_ROM}"
    )
    target_sources(${projectname} PRIVATE "${EMBEDDED_ROM_C}")
    target_compile_definitions(${projectname} PRIVATE EMBEDDED_NES_ROM=1)
    message(STATUS "Embedded ROM: ${EMBED_NES_ROM}")
endif()

pico_set_program_name(${projectname} "${projectname}")
pico_set_program_version(${projectname} "0.8")

# ---------------------------------------------------------------------------
# stdio routing
# ---------------------------------------------------------------------------
if(UART_ENABLED EQUAL 0)
    message("Disabling UART output for this board.")
    pico_enable_stdio_uart(${projectname} 0)
else()
    pico_enable_stdio_uart(${projectname} 1)
endif()
pico_enable_stdio_usb(${projectname} 0)

# ---------------------------------------------------------------------------
# What family_configure_target() used to do for us, inlined:
#   * RP2040-E5 USB device enumeration fix
#   * Silence harmless warnings from TinyUSB / pico-pio-usb sources
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

target_compile_options(${projectname} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-cast-function-type
    -Wno-maybe-uninitialized
)

# ---------------------------------------------------------------------------
# Project compile definitions (board pins, features, etc.)
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PRIVATE
    DVICONFIG=${DVICONFIG}
    SDCARD_PIN_CS=${SD_CS}
    SDCARD_PIN_SCK=${SD_SCK}
    SDCARD_PIN_MOSI=${SD_MOSI}
    SDCARD_PIN_MISO=${SD_MISO}
    SDCARD_PIO=pio1                 # Fallback when SPI initialization fails
    SDCARD_SPI=${SD_SPI}
    NES_PIN_CLK=${NES_CLK}
    NES_PIN_DATA=${NES_DATA}
    NES_PIN_LAT=${NES_LAT}
    NES_PIO=${NES_PIO}
    NES_PIN_CLK_1=${NES_CLK_1}
    NES_PIN_DATA_1=${NES_DATA_1}
    NES_PIN_LAT_1=${NES_LAT_1}
    NES_PIO_1=${NES_PIO_1}
    WII_PIN_SDA=${WII_SDA}
    WII_PIN_SCL=${WII_SCL}
    ZAPPER_D3=${ZAPPER_D3}
    ZAPPER_D4=${ZAPPER_D4}
    ZAPPER_INVERT_D3=${ZAPPER_INVERT_D3}
    ZAPPER_INVERT_D4=${ZAPPER_INVERT_D4}
    ZAPPER_DEBUG=${ZAPPER_DEBUG}
    ZAPPER_MEASURE=${ZAPPER_MEASURE}
    ZAPPER_MEASURE_WHITE=${ZAPPER_MEASURE_WHITE}
    LED_GPIO_PIN=${LED_GPIO_PIN}
    HW_CONFIG=${HW_CONFIG}
    WIIPAD_I2C=${WIIPAD_I2C}
    USE_I2S_AUDIO=${USE_I2S_AUDIO}
    PICO_AUDIO_I2S_DATA_PIN=${PICO_AUDIO_I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${PICO_AUDIO_I2S_CLOCK_PIN_BASE}
    PICO_AUDIO_I2S_PIO=${PICO_AUDIO_I2S_PIO}
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED=${PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED}
    PICO_AUDIO_I2S_RESET_PIN=${PICO_AUDIO_I2S_RESET_PIN}
    PICO_AUDIO_I2S_INTERRUPT_PIN=${PICO_AUDIO_I2S_INTERRUPT_PIN}
    PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON=${PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON}
    CFG_TUH_RPI_PIO_USB=${ENABLE_PIO_USB}
    PIO_USB_USE_PIO=${PIO_USB_USE_PIO}
    PSRAM_CS_PIN=${PSRAM_CS_PIN}
    GPIOHSTXD0=${GPIOHSTXD0}
    GPIOHSTXD1=${GPIOHSTXD1}
    GPIOHSTXD2=${GPIOHSTXD2}
    GPIOHSTXCK=${GPIOHSTXCK}
    GPIOHSTXINVERTED=${GPIOHSTXINVERTED}
    ENABLE_VU_METER=${ENABLE_VU_METER}
    WIIPAD_DELAYED_START=${WIIPAD_DELAYED_START}
    ENABLEDVI=1
    DOUBLEFRAMEBUFFER=0
    NES_OVERCLOCK_FIX=1
    #HSTX_DEBUG=1
)

if(NOT PICO_PLATFORM STREQUAL "rp2040")
    message(STATUS "Using PICO_PIO_USE_GPIO_BASE=1 for non-rp2040 platforms.")
    target_compile_definitions(${projectname} PRIVATE
        PICO_PIO_USE_GPIO_BASE=1
        PICO_STACK_SIZE=3072
    )
endif()

if(DEFINED PICO_DEFAULT_UART_TX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_TX_PIN: ${PICO_DEFAULT_UART_TX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_TX_PIN=${PICO_DEFAULT_UART_TX_PIN}
        PICO_DEFAULT_UART=0
    )
endif()

if(DEFINED PICO_DEFAULT_UART_RX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_RX_PIN: ${PICO_DEFAULT_UART_RX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_RX_PIN=${PICO_DEFAULT_UART_RX_PIN}
    )
endif()

if(DEFINED PICO_DEFAULT_UART)
    message(STATUS "Using PICO_DEFAULT_UART: ${PICO_DEFAULT_UART}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART=${PICO_DEFAULT_UART}
    )
endif()

# ---------------------------------------------------------------------------
# PIO USB (optional)
# tinyusb_pico_pio_usb is a target the pico-sdk exposes when TinyUSB is present.
# ---------------------------------------------------------------------------
if(ENABLE_PIO_USB EQUAL 1)
    if(NOT PIO_DP_PLUS_PIN EQUAL -1)
        message(STATUS "Using PIO USB DP pin: ${PIO_DP_PLUS_PIN}")
        target_compile_definitions(${projectname} PRIVATE
            PICO_DEFAULT_PIO_USB_DP_PIN=${PIO_DP_PLUS_PIN}
        )
    endif()
    message("Adding target link libraries for PIO USB support.")
    target_link_libraries(${projectname} PRIVATE tinyusb_pico_pio_usb)
endif()

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
target_link_libraries(${projectname} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_interp
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_flash
    dvi
    util
    infones
    tinyusb_host
    tinyusb_board
    pico_shared
    xinput_host
)

if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    target_link_libraries(${projectname} PRIVATE pico_cyw43_arch_none)
endif()

target_include_directories(${projectname} PRIVATE
    pico_lib
    pico_shared
    infones
    tusb_xinput
    .
)

add_subdirectory(infones)
add_subdirectory(pico_lib/dvi)
add_subdirectory(pico_lib/util)
add_subdirectory(pico_shared)
add_subdirectory(tusb_xinput xinput_host)

target_link_options(${projectname} PRIVATE -Xlinker --print-memory-usage --data-sections)

# Optional: relink this emulator to run from a bootloader-managed partition
# (for use with pico_bootLoader). Off by default so standalone builds are
# unchanged.
#   -DBUILD_FOR_BOOTLOADER=ON                       -> legacy single partition
#                                                       == slot 0 on slot-capable boards
#   -DBUILD_FOR_BOOTLOADER=ON  -DBUILD_FOR_BOOTLOADER_SLOT=N -> pinned slot N
if(BUILD_FOR_BOOTLOADER)
    include("pico_shared/BootPartition.cmake")
    if(DEFINED BUILD_FOR_BOOTLOADER_SLOT)
        frens_link_to_pinned_slot(${projectname} ${BUILD_FOR_BOOTLOADER_SLOT})
    else()
        frens_offset_for_bootloader(${projectname})
    endif()
endif()

pico_add_extra_outputs(${projectname})
