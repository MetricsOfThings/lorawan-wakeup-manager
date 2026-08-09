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

add_executable(vault_stm32u031
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/gcc/startup_stm32u031xx.s
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/system_stm32u0xx.c
    src/main.c
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

add_custom_command(TARGET vault_stm32u031 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_stm32u031>
)
