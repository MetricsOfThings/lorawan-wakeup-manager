add_library(vault_core STATIC
    src/vault_i2c_registers.c
    src/vault_core.c
)

target_include_directories(vault_core PUBLIC include)

target_compile_definitions(vault_core PUBLIC
    VAULT_CONTEXT_SIZE=${VAULT_CONTEXT_SIZE}
    VAULT_I2C_ADDR=0x42
    VAULT_DEFAULT_WAKE_INTERVAL_SEC=60
)

# vault_core is shared across all three backends (host, lpc810, stm32u031)
# and has no compile options of its own by default, so it would build in
# ARM mode on the Cortex-M0+ targets and fail to link against Thumb
# objects ("Unknown destination type (ARM/Thumb)"). VAULT_TARGET is set
# by the top-level CMakeLists.txt before this subdirectory is added, so
# it's available here; key the flags off it directly instead of
# duplicating `target_compile_options(vault_core PRIVATE ...)` in both
# platform/lpc810/CMakeLists.txt and platform/stm32u031/CMakeLists.txt.
# -Os also fixes vault_core being built at -O0 (CMake's implicit default
# with no explicit optimization flag) on both firmware targets, where
# flash budget is tight (4 KB on LPC810).
if(VAULT_TARGET STREQUAL "lpc810" OR VAULT_TARGET STREQUAL "stm32u031")
    target_compile_options(vault_core PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
endif()
