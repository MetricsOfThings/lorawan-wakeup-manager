set(GECKO_SDK_ROOT ${CMAKE_SOURCE_DIR}/vendor/Gecko_SDK)

# Paths confirmed to exist in the sparse-checked-out Gecko SDK tree by
# Task 1's `git ls-tree` / `find` verification (see task-1-report.md):
#   - CMSIS core (Cortex-M3):        platform/CMSIS/Core/Include
#   - EFM32G210F128 device header:   platform/Device/SiliconLabs/EFM32G/Include
#     (Silicon Labs groups the whole EFM32G family -- G200/G210/G222/...
#     -- under one "EFM32G" device directory, not a per-part directory;
#     the F128 part is selected at compile time via the EFM32G210F128
#     macro, consumed by that directory's em_device.h.)
#   - emlib headers/sources:         platform/emlib/inc, platform/emlib/src
#   - common headers emlib depends on (sl_assert.h, sl_common.h,
#     sl_enum.h, sl_status.h -- not part of the brief's original guessed
#     path list, discovered by tracing em_cmu.h/em_gpio.h/em_emu.h/
#     em_bus.h #includes before checkout): platform/common/inc
set(EFM32G210_INCLUDE_DIRS
    ${GECKO_SDK_ROOT}/platform/CMSIS/Core/Include
    ${GECKO_SDK_ROOT}/platform/Device/SiliconLabs/EFM32G/Include
    ${GECKO_SDK_ROOT}/platform/emlib/inc
    ${GECKO_SDK_ROOT}/platform/common/inc
)

add_library(vault_efm32g210_sdk_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_efm32g210_sdk_check PRIVATE
    ${EFM32G210_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Confirmed against platform/Device/SiliconLabs/EFM32G/Include/em_device.h:
# this file selects the part header purely via #if defined(EFM32G210F128),
# no other spelling accepted.
target_compile_definitions(vault_efm32g210_sdk_check PRIVATE EFM32G210F128)
target_compile_options(vault_efm32g210_sdk_check PRIVATE -mcpu=cortex-m3 -mthumb)
