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

# Task 2: linker script, startup file, and the real executable target.
#
# src/startup_efm32g210.c is a new, hand-written startup file, NOT the
# vendored platform/Device/SiliconLabs/EFM32G/Source/GCC/startup_efm32g.S.
# That vendored file exists and was read, but is written for the CMSIS/
# gcc-arm-none-eabi-library startup convention (__StackTop, a
# __copy_table_start__/__copy_table_end__ table, a
# __zero_table_start__/__zero_table_end__ table, a .vectors section name,
# and an indirect entry via __START/_start normally supplied by newlib
# crt0) -- none of which match this project's simpler, linker-script-only
# convention (_sidata/_sdata/_edata/_sbss/_ebss/_estack symbols, .isr_vector
# section, Reset_Handler calls main() directly) shared by the LPC810 and
# STM32U031 backends' hand-written startup files, and required by the
# linker script given for this task (efm32g210f128.ld below). Rather than
# rewriting that linker script to match the vendored file's conventions,
# src/startup_efm32g210.c was written directly, following the same
# weak-alias-to-Default_Handler pattern as platform/lpc810/src/startup_lpc810.c.
# Its vector table content (IRQ order/names) was taken verbatim from the
# vendored startup_efm32g.S as the authoritative source, then cross-checked
# against this exact part's IRQn_Type enum in efm32g210f128.h -- see the
# comments in startup_efm32g210.c for the (confirmed) gaps between the two.
#
# system_efm32g.c (SystemInit()/SystemCoreClock) is intentionally NOT
# compiled into this target yet: nothing in this task's scope (this empty
# main() and the startup file) calls SystemInit() or reads SystemCoreClock.
# It will need to be added when clock init (Task 4) starts calling emlib
# CMU functions that depend on it.
#
# SysTick_Handler: the vector table below defines it (weak alias to
# Default_Handler, matching the other two backends), but no real handler
# is implemented. Checked: the only SysTick-touching code in the vendored
# emlib (platform/emlib/src/em_cmu.c, SysTick->CTRL clock-source-select
# bit pokes) is gated to Series 2 parts (_SILICON_LABS_32B_SERIES_2_CONFIG_1)
# and never enables the SysTick exception or busy-waits on it counting --
# EFM32G210 is a Series 0 part, so that code path is dead here. No emlib
# function reachable from this backend's later tasks (clock init, I2C)
# was found to depend on a live SysTick interrupt.
# Task 3: em_gpio.c/em_cmu.c/em_core.c are real emlib *source* files, not
# just headers -- unlike the LPC810/STM32U031 backends' raw-register or
# HAL-struct-based GPIO code, GPIO_PinModeSet()/GPIO_PinOutSet()/
# GPIO_PinOutClear() and CMU_ClockEnable() (called from
# platform_efm32g210_gpio.c) are declared non-inline in
# vendor/Gecko_SDK/platform/emlib/inc/{em_gpio.h,em_cmu.h} for this
# Series-0 part (confirmed: em_cmu.h's __STATIC_INLINE CMU_ClockEnable
# stub only exists under #if defined(_SILICON_LABS_32B_SERIES_2_CONFIG_1),
# which EFM32G210 does not define), so their real definitions must be
# compiled in or the link fails with "undefined reference". em_core.c is
# needed transitively: em_gpio.c's BUS_RegMaskedWrite() (from em_bus.h)
# calls CORE_EnterCritical()/CORE_ExitCritical(), defined in em_core.c.
# All three compiled cleanly against this sparse checkout's vendored
# headers with no additional missing-header surprises (em_cmu.c's own
# #includes -- em_syscfg.h, em_msc.h, sl_component_catalog.h, etc. --
# were already present or conditionally compiled out for this part).
add_executable(vault_efm32g210
    src/startup_efm32g210.c
    src/main.c
    src/platform_efm32g210_gpio.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_gpio.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_cmu.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_core.c
)

target_include_directories(vault_efm32g210 PRIVATE
    ${EFM32G210_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # core/include: needed starting this task for vault/platform.h
    # (platform_main_rail_enable/platform_bus_isolate declarations,
    # included by platform_efm32g210_gpio.c). vault_core itself is still
    # not linked -- same "headers now, link later" precedent LPC810's
    # CMakeLists.txt set at its own equivalent GPIO task.
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_definitions(vault_efm32g210 PRIVATE EFM32G210F128)
target_compile_options(vault_efm32g210 PRIVATE -mcpu=cortex-m3 -mthumb -Os)
target_link_options(vault_efm32g210 PRIVATE
    -mcpu=cortex-m3 -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/efm32g210f128.ld
    -Wl,-Map=vault_efm32g210.map
)

# Raw -T flag isn't tracked as a link input by CMake on its own -- same
# fix both other backends' CMakeLists.txt needed.
set_target_properties(vault_efm32g210 PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker/efm32g210f128.ld
)

# vault_core is not linked yet: platform_init() has no efm32g210
# implementation until Task 3 onward, and main.c doesn't call into
# vault_core yet either (see src/main.c). Matches the precedent set by
# both other backends, which added this link only once a real caller
# existed (LPC810: Task 9, once platform_lpc810_i2c.c needed
# vault_i2c_registers_on_*()).

add_custom_command(TARGET vault_efm32g210 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_efm32g210>
)
