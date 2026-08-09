cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VAULT_TARGET)
    set(VAULT_TARGET "host" CACHE STRING "Build target: host, lpc810, stm32u031")
endif()

if(VAULT_TARGET STREQUAL "lpc810" OR VAULT_TARGET STREQUAL "stm32u031")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake" CACHE FILEPATH "")
endif()

project(lorawan_wakeup_manager C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED VAULT_CONTEXT_SIZE)
    if(VAULT_TARGET STREQUAL "lpc810")
        # 320 = RadioLib's actual persisted-state requirement:
        # RADIOLIB_LORAWAN_NONCES_BUF_SIZE (14) +
        # RADIOLIB_LORAWAN_SESSION_BUF_SIZE (302) = 316, rounded up.
        # Comfortably fits the LPC810's 1 KB SRAM (596 B of 1024 B used
        # at this size, leaving headroom beyond the reserved 256 B
        # stack) -- the linker-enforced absolute ceiling is 746 B, but
        # that leaves no real safety margin, so this uses a real
        # library's requirement instead of the theoretical max.
        set(VAULT_CONTEXT_SIZE 320)
    elseif(VAULT_TARGET STREQUAL "stm32u031")
        set(VAULT_CONTEXT_SIZE 128)
    else()
        set(VAULT_CONTEXT_SIZE 8)
    endif()
endif()

enable_testing()

add_subdirectory(core)

if(VAULT_TARGET STREQUAL "host")
    add_subdirectory(platform/host_mock)
    add_subdirectory(tests)
elseif(VAULT_TARGET STREQUAL "lpc810")
    add_subdirectory(platform/lpc810)
elseif(VAULT_TARGET STREQUAL "stm32u031")
    add_subdirectory(platform/stm32u031)
else()
    message(FATAL_ERROR "Unknown VAULT_TARGET: ${VAULT_TARGET} (expected host, lpc810, or stm32u031)")
endif()
