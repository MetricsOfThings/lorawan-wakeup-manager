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
