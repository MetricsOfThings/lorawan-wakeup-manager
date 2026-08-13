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
