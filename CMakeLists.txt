cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VAULT_TARGET)
    set(VAULT_TARGET "host" CACHE STRING "Build target: host, lpc810, stm32u031, efm32g210, stm32c011")
endif()

if(VAULT_TARGET STREQUAL "lpc810" OR VAULT_TARGET STREQUAL "stm32u031" OR VAULT_TARGET STREQUAL "efm32g210" OR VAULT_TARGET STREQUAL "stm32c011")
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
        # Matches the LPC810 default above for consistency: the same
        # RadioLib session data should fit on either Vault variant.
        # Trivial against this part's 12 KB SRAM (1496 of 12288 bytes
        # used at this size, confirmed by build).
        set(VAULT_CONTEXT_SIZE 320)
    elseif(VAULT_TARGET STREQUAL "efm32g210")
        # Matches both other hardware backends for consistency -- same
        # RadioLib session data should fit on any Vault variant.
        # Trivial against this part's 16 KB SRAM (1392 of 16384 bytes
        # used at this size, confirmed by build).
        set(VAULT_CONTEXT_SIZE 320)
    elseif(VAULT_TARGET STREQUAL "stm32c011")
        # Matches all three other hardware backends for consistency --
        # same RadioLib session data should fit on any Vault variant.
        # Trivial against this part's 6 KB SRAM, the tightest budget of
        # any backend so far, confirmed by build.
        set(VAULT_CONTEXT_SIZE 320)
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
elseif(VAULT_TARGET STREQUAL "efm32g210")
    add_subdirectory(platform/efm32g210)
elseif(VAULT_TARGET STREQUAL "stm32c011")
    add_subdirectory(platform/stm32c011)
else()
    message(FATAL_ERROR "Unknown VAULT_TARGET: ${VAULT_TARGET} (expected host, lpc810, stm32u031, efm32g210, or stm32c011)")
endif()
