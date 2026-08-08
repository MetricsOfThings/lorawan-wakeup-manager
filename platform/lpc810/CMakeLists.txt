add_library(vault_lpc810_cmsis_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_lpc810_cmsis_check PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
)

target_compile_options(vault_lpc810_cmsis_check PRIVATE -mcpu=cortex-m0plus -mthumb)
