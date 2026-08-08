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
)

target_include_directories(vault_lpc810 PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_options(vault_lpc810 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_lpc810 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc810.ld
    -Wl,-Map=vault_lpc810.map
)

add_custom_command(TARGET vault_lpc810 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_lpc810>
)
