add_library(vault_lpc810_cmsis_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_lpc810_cmsis_check PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
)

target_compile_options(vault_lpc810_cmsis_check PRIVATE -mcpu=cortex-m0plus -mthumb)

add_executable(vault_lpc810
    src/startup_lpc810.c
    src/main.c
    src/platform_lpc810_gpio.c
    src/platform_lpc810_timer.c
    src/platform_lpc810_i2c.c
    src/platform_lpc810_power.c
)

target_include_directories(vault_lpc810 PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/core/include
)

# platform_lpc810_i2c.c calls vault_i2c_registers_on_*(), defined in
# vault_core; link it in now that the I2C driver needs it (Task 9).
# vault_core itself has no per-target compile options (it's shared with
# host_mock and, eventually, stm32u031), so it defaults to ARM mode;
# force Thumb/Cortex-M0+ codegen here to match vault_lpc810's other
# objects, or the link fails with "Unknown destination type (ARM/Thumb)".
target_link_libraries(vault_lpc810 PRIVATE vault_core)
target_compile_options(vault_core PRIVATE -mcpu=cortex-m0plus -mthumb)

target_compile_options(vault_lpc810 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_lpc810 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc810.ld
    -Wl,-Map=vault_lpc810.map
)

add_custom_command(TARGET vault_lpc810 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_lpc810>
)
