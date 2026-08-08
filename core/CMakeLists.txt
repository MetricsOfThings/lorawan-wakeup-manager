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
