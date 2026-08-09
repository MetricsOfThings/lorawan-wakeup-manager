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

add_custom_command(TARGET vault_stm32u031 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_stm32u031>
)
