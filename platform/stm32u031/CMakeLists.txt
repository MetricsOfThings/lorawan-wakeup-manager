set(STM32CUBE_U0_ROOT ${CMAKE_SOURCE_DIR}/vendor/STM32CubeU0)

set(STM32_INCLUDE_DIRS
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Include
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Include
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Inc
)

add_library(vault_stm32u031_hal_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_stm32u031_hal_check PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vault_stm32u031_hal_check PRIVATE STM32U031xx)
target_compile_options(vault_stm32u031_hal_check PRIVATE -mcpu=cortex-m0plus -mthumb)

set(STM32_HAL_DRIVER_SOURCES
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_cortex.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_gpio.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rcc_ex.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_pwr_ex.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rtc.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_rtc_ex.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_i2c.c
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_i2c_ex.c
)

add_executable(vault_stm32u031
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/gcc/startup_stm32u031xx.s
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/system_stm32u0xx.c
    ${STM32_HAL_DRIVER_SOURCES}
    src/main.c
    src/platform_stm32u031.c
    src/libc_stubs.c
)

target_include_directories(vault_stm32u031 PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vault_stm32u031 PRIVATE STM32U031xx)
target_compile_options(vault_stm32u031 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_stm32u031 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32u031f8.ld
    -Wl,-Map=vault_stm32u031.map
)

# See the matching comment in platform/lpc810/CMakeLists.txt: a raw -T
# flag isn't tracked as a link input by CMake on its own.
set_target_properties(vault_stm32u031 PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32u031f8.ld
)

# vault_core's own -mcpu/-mthumb/-Os compile options are set in
# core/CMakeLists.txt, keyed on VAULT_TARGET, rather than duplicated here
# (the same fix Task 9 needed for vault_lpc810, now consolidated).
target_link_libraries(vault_stm32u031 PRIVATE vault_core)

# VAULT_LOG_ENABLED (core/CMakeLists.txt) drives core/'s vault_log()
# calls; this backend's implementation uses USART2 on PA2, which -
# unlike the LPC810 backend - costs no pin trade-off (this part's 16
# usable I/Os leave PA2 free already).
if(VAULT_LOG_ENABLED)
    target_sources(vault_stm32u031 PRIVATE
        src/platform_stm32u031_uart.c
        ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Src/stm32u0xx_hal_uart.c
    )
endif()

# Diagnostic only, off by default: keeps DBGMCU's debug clock alive
# through Stop mode (DBGMCU_CR_DBG_STOP), so a debugger can connect and
# read registers while the chip is genuinely asleep -- normally SWD
# itself loses its clock domain in Stop 2, which is why connecting
# fails ("stalled AP operation" / "Examination failed") against a
# build without this. Mutually exclusive in practice with an actual
# current measurement: this bit itself draws extra power while
# enabled, so build WITHOUT it for a clean reading and WITH it only
# when you specifically need to inspect registers mid-sleep (pair with
# VAULT_DIAGNOSTIC_SKIP_I2C_WAIT to reach sleep without a host).
option(VAULT_DIAGNOSTIC_DEBUG_IN_STOP
    "Keep the debug clock alive through Stop mode so a debugger can inspect registers while asleep -- costs extra power, not for a real current measurement"
    OFF)
if(VAULT_DIAGNOSTIC_DEBUG_IN_STOP)
    target_compile_definitions(vault_stm32u031 PRIVATE VAULT_DIAGNOSTIC_DEBUG_IN_STOP)
endif()

# Diagnostic only, off by default: toggles PA1 (confirmed unused by
# this backend -- no signal, no pull, safe to repurpose) high right
# before HAL_PWREx_EnterSTOP2Mode() and low right after it returns,
# so a current-profiling tool with no debugger involved at all (e.g.
# a Nordic PPK2) can see, directly on the same capture as the current
# waveform, exactly how long the code actually spends inside the
# Stop-2 call -- see the ~900uA/~800uA/~1.27mA Stop-mode current
# investigation in git history for why this was needed: register
# inspection via a debugger halt turned out to be unreliable evidence
# here, since halting itself can be a WFI wake condition.
option(VAULT_DIAGNOSTIC_SLEEP_GPIO_MARKER
    "Toggle PA1 around Stop 2 entry/exit for debugger-free timing correlation with an external current profiler"
    OFF)
if(VAULT_DIAGNOSTIC_SLEEP_GPIO_MARKER)
    target_compile_definitions(vault_stm32u031 PRIVATE VAULT_DIAGNOSTIC_SLEEP_GPIO_MARKER)
endif()

# Off by default -- gates a real (not diagnostic-only) low-power lever
# behind a flag while still under active iteration. PA13/PA14 (SWDIO/
# SWCLK) default to AF0 with their debug pull resistors enabled
# specifically so a debugger can always reconnect out of the box; a
# live pulldown-probe test during the Stop 2 current investigation
# found both pins still active during sleep, and
# HAL_PWREx_EnterSTOP2Mode()'s own doc comment confirms Stop 2 leaves
# all I/O pins exactly as they were in Run -- so the SWD pull
# resistors and input buffers stay live for the whole sleep interval
# unless firmware isolates them, same lever as the LPC810/STM32
# "every unused pin to Analog" rule already applied everywhere else.
# Matches the reference "sleep3" firmware's approach exactly: blanket
# every pin (including SWD) to Analog ONCE at boot, not just around
# each sleep cycle -- so once this is on, SWD is gone for the rest of
# that boot, not just during sleep. Enabling this generally means you
# can no longer reconnect a debugger to reflash except by forcing the
# ROM bootloader via BOOT0 (see the BOOT0 pull-down added during the
# same investigation) -- so leave this off during normal development
# and only turn it on for a deliberate final low-power measurement/
# production build.
option(VAULT_DIAGNOSTIC_DISABLE_SWD_IN_SLEEP
    "Blanket every GPIOA pin (including SWDIO/SWCLK) to Analog once at boot -- real power saving, but generally costs debugger reconnect for the rest of that boot (use BOOT0 bootloader recovery instead)"
    OFF)
if(VAULT_DIAGNOSTIC_DISABLE_SWD_IN_SLEEP)
    target_compile_definitions(vault_stm32u031 PRIVATE VAULT_DIAGNOSTIC_DISABLE_SWD_IN_SLEEP)
endif()

# Diagnostic only, off by default: switches the RTC clock source from
# LSI to LSE, purely to directly A/B-test whether LSE itself accounts
# for any of the remaining gap to a known-good reference firmware's
# Stop 2 current (that reference firmware has only ever been tested
# with LSI on this hardware -- LSE has NOT been confirmed as the
# explanation, this option exists to actually test it rather than
# guess). A much earlier board revision in this project's history
# found LSE non-functional (garbled UART at both drive levels) -- this
# option makes it easy to try again on current hardware and easy to
# revert if it fails, without permanently committing to LSE in
# rtc_init(). Requires a 32.768 kHz crystal on OSC32_IN/OSC32_OUT.
option(VAULT_DIAGNOSTIC_RTC_LSE
    "Use LSE instead of LSI for the RTC clock source -- diagnostic A/B test only, requires a fitted 32.768kHz crystal"
    OFF)
if(VAULT_DIAGNOSTIC_RTC_LSE)
    target_compile_definitions(vault_stm32u031 PRIVATE VAULT_DIAGNOSTIC_RTC_LSE)
endif()

add_custom_command(TARGET vault_stm32u031 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_stm32u031>
)
