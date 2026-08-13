set(STM32CUBE_C0_ROOT ${CMAKE_SOURCE_DIR}/vendor/STM32CubeC0)

# Real paths confirmed in Task 1 Step 2 (see .superpowers/sdd/task-1-report.md).
set(STM32_INCLUDE_DIRS
    ${STM32CUBE_C0_ROOT}/Drivers/CMSIS/Include
    ${STM32CUBE_C0_ROOT}/Drivers/CMSIS/Device/ST/STM32C0xx/Include
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Inc
)

add_library(vault_stm32c011_hal_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_stm32c011_hal_check PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# STM32C011xx confirmed in Step 2 (Drivers/CMSIS/Device/ST/STM32C0xx/Include/stm32c011xx.h).
target_compile_definitions(vault_stm32c011_hal_check PRIVATE STM32C011xx)
target_compile_options(vault_stm32c011_hal_check PRIVATE -mcpu=cortex-m0plus -mthumb)

# Real vault_stm32c011 executable target (Task 2). Flash/RAM sizes (32 KB /
# 6 KB) confirmed against stm32c011xx.h's FLASH_SIZE_DEFAULT/SRAM_SIZE_MAX
# -- see linker/stm32c011j6.ld's header comment.
set(STM32_HAL_DRIVER_SOURCES
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_cortex.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_gpio.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_rcc.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_rcc_ex.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_pwr.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_pwr_ex.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_rtc.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_rtc_ex.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_i2c.c
    ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_i2c_ex.c
)

# Vendored startup file: only one CMSIS Device-tree copy exists for this
# family/macro (STM32C011xx), unlike per-package startup files found
# elsewhere under Projects/ (e.g. startup_stm32c011f6ux.s) -- confirmed
# via `find vendor/STM32CubeC0/Drivers/CMSIS/Device -iname
# 'startup_stm32c011*.s'` in Task 2.
add_executable(vault_stm32c011
    ${STM32CUBE_C0_ROOT}/Drivers/CMSIS/Device/ST/STM32C0xx/Source/Templates/gcc/startup_stm32c011xx.s
    ${STM32CUBE_C0_ROOT}/Drivers/CMSIS/Device/ST/STM32C0xx/Source/Templates/system_stm32c0xx.c
    ${STM32_HAL_DRIVER_SOURCES}
    src/main.c
    src/platform_stm32c011.c
    src/libc_stubs.c
)

target_include_directories(vault_stm32c011 PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vault_stm32c011 PRIVATE STM32C011xx)
target_compile_options(vault_stm32c011 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_stm32c011 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32c011j6.ld
    -Wl,-Map=vault_stm32c011.map
)

# See the matching comment in platform/lpc810/CMakeLists.txt: a raw -T
# flag isn't tracked as a link input by CMake on its own.
set_target_properties(vault_stm32c011 PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32c011j6.ld
)

# vault_core's own -mcpu/-mthumb/-Os compile options are set in
# core/CMakeLists.txt, keyed on VAULT_TARGET, rather than duplicated here.
target_link_libraries(vault_stm32c011 PRIVATE vault_core)

# VAULT_LOG_ENABLED (core/CMakeLists.txt) drives core/'s vault_log()
# calls; this backend's implementation repurposes PA14/SWCLK as
# USART2_TX (see src/platform_stm32c011_uart.c's file comment for the
# datasheet correction against the design spec's original PA13/SWDIO
# assumption), since this SO8N part's pin map is otherwise fully
# committed (see the pin-map comment above MAIN_RAIL_EN_PORT in
# src/platform_stm32c011.c).
if(VAULT_LOG_ENABLED)
    target_sources(vault_stm32c011 PRIVATE
        src/platform_stm32c011_uart.c
        ${STM32CUBE_C0_ROOT}/Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal_uart.c
    )
endif()

add_custom_command(TARGET vault_stm32c011 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_stm32c011>
)
