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
# (system_efm32g.c was deferred here at Task 2/3 -- see the Task 4
# comment below for why and when it was actually added.)
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
#
# Task 4: em_rtc.c is likewise a real emlib *source* file, not just a
# header, for this part -- RTC_Init()/RTC_Enable()/RTC_CompareSet() are
# declared without __STATIC_INLINE in vendor/Gecko_SDK/platform/emlib/
# inc/em_rtc.h (only RTC_IntEnable/RTC_IntClear/RTC_CounterGet/etc. are
# header-only inline). em_rtc.c's own #includes (em_bus.h, sl_assert.h)
# are already satisfied by this target's existing include dirs/sources.
#
# Task 4 also required adding system_efm32g.c (SystemCoreClock/
# SystemCoreClockGet()/SystemMaxCoreClockGet()), which this CMakeLists.txt
# previously (Task 2/3) deliberately left out with a comment that nothing
# in scope yet called into emlib CMU code depending on it. That's no
# longer true: efm32g210_clock_init() (platform_efm32g210.c) calls
# CMU_ClockSelectSet() and CMU_ClockDivSet(), and linking without
# system_efm32g.c failed with "undefined reference to
# SystemCoreClockGet"/"SystemMaxCoreClockGet" from em_cmu.c -- confirmed
# by actually attempting the link, not assumed in advance. Note this file
# does NOT call SystemInit() anywhere (startup_efm32g210.c's Reset_Handler
# does not either) -- only the two clock-query functions it defines are
# needed as link-time dependencies of em_cmu.c; SystemCoreClock itself
# stays at this file's static-initializer default (14 MHz, the part's
# reset HFRCO frequency) until/unless something calls SystemCoreClockGet()
# to refresh it, which is a pre-existing gap this task did not introduce
# and does not fix.
# Task 5: platform_efm32g210_i2c.c implements I2C0 in slave mode entirely
# via direct register access (I2C0->CTRL/SADDR/ROUTE/IEN/IF/IFC/RXDATA/
# TXDATA), the same "closer to raw register access than HAL-callback"
# style the design spec (§6) anticipated for this backend -- it does not
# call any of em_i2c.h's non-inline functions (I2C_Init/I2C_Enable/
# I2C_BusFreqSet/I2C_Transfer/I2C_TransferInit/I2C_Reset are all written
# for master-mode polled transfers, per that same header, and none are
# used here). So unlike em_gpio.c/em_cmu.c/em_core.c/em_rtc.c above,
# em_i2c.c is NOT added to target_sources: nothing in
# platform_efm32g210_i2c.c calls a non-inline em_i2c.h function, so
# there is no "undefined reference" to fix (checked by attempting the
# link without it, same discipline as Task 4's system_efm32g.c addition).
add_executable(vault_efm32g210
    src/startup_efm32g210.c
    src/main.c
    src/platform_efm32g210_gpio.c
    src/platform_efm32g210.c
    src/platform_efm32g210_i2c.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_gpio.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_cmu.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_core.c
    ${GECKO_SDK_ROOT}/platform/emlib/src/em_rtc.c
    ${GECKO_SDK_ROOT}/platform/Device/SiliconLabs/EFM32G/Source/system_efm32g.c
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

# platform_efm32g210_i2c.c calls vault_i2c_registers_on_*(), defined in
# vault_core; link it in now that the I2C driver needs it (Task 5), the
# same point (once a real caller exists) both other backends added this
# link at (LPC810: Task 9, once platform_lpc810_i2c.c needed
# vault_i2c_registers_on_*()). vault_core's own -mcpu/-mthumb/-Os compile
# options are set in core/CMakeLists.txt, keyed on VAULT_TARGET, rather
# than duplicated here.
target_link_libraries(vault_efm32g210 PRIVATE vault_core)

add_custom_command(TARGET vault_efm32g210 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_efm32g210>
)
