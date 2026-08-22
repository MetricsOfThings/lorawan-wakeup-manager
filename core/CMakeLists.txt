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

# Application-level logging (vault_log()/vault_log_u32(), see
# vault/vault_log.h) is opt-in: off by default, since on the LPC810
# backend enabling it means giving up the dedicated RESET pin (see
# platform/lpc810/CMakeLists.txt). PUBLIC so every backend that links
# vault_core sees the same VAULT_LOG_ENABLED definition, whether or not
# it needs to provide a vault_log() implementation.
option(VAULT_LOG_ENABLED "Enable application-level logging via vault_log()" OFF)
if(VAULT_LOG_ENABLED)
    target_sources(vault_core PRIVATE src/vault_log.c)
    target_compile_definitions(vault_core PUBLIC VAULT_LOG_ENABLED)
endif()

# Diagnostic only, off by default: skips the WAKE_MAIN wait loop's I2C
# command requirement so vault_core_step() proceeds straight to bus
# isolation and sleep with no host connected at all -- for isolating a
# backend's own Stop-mode current consumption on a bench supply, with
# nothing else attached to the board (see the STM32U031 Stop 2 current
# investigation in git history for why this was needed: verifying
# whether real hardware's Stop-mode current is coming from the MCU
# itself or from something else sharing its board/rail requires being
# able to reach sleep without a working host/I2C exchange at all).
# Never enable for a real deployment -- with this on, the vault will
# power its main rail on, then immediately cut it again and sleep,
# without giving whatever's on that rail any chance to actually run.
option(VAULT_DIAGNOSTIC_SKIP_I2C_WAIT
    "Skip the WAKE_MAIN I2C wait loop and sleep immediately -- for isolating Stop-mode current consumption, not for real deployment"
    OFF)
if(VAULT_DIAGNOSTIC_SKIP_I2C_WAIT)
    target_compile_definitions(vault_core PUBLIC VAULT_DIAGNOSTIC_SKIP_I2C_WAIT)
endif()

# Diagnostic only, off by default, and only meaningful combined with
# VAULT_DIAGNOSTIC_SKIP_I2C_WAIT: skips WAKE_MAIN's I2C peripheral
# bring-up/main-rail-enable and BUS_ISOLATION's corresponding teardown
# entirely, going straight from wake to ARM_SLEEP with no peripheral
# touched at all besides the platform's own RTC/PWR/GPIO low-power
# setup -- isolates whether a backend's brief per-cycle I2C init/
# deinit (harmless in Run, but happening every single cycle) has any
# measurable effect on steady Stop-mode current, by comparing directly
# against a minimal reference sleep firmware with no peripherals at
# all beyond RTC/PWR/GPIO. See the STM32U031 Stop 2 current
# investigation in git history. Never enable for a real deployment --
# with this on, the vault never talks to a host at all.
option(VAULT_DIAGNOSTIC_SKIP_I2C_PERIPHERAL
    "Skip I2C peripheral bring-up/teardown and main rail enable entirely -- isolates whether per-cycle I2C init/deinit affects Stop-mode current, not for real deployment"
    OFF)
if(VAULT_DIAGNOSTIC_SKIP_I2C_PERIPHERAL)
    target_compile_definitions(vault_core PUBLIC VAULT_DIAGNOSTIC_SKIP_I2C_PERIPHERAL)
endif()

# Diagnostic only, off by default: bypasses vault_core_init()/
# vault_core_step() entirely -- see VAULT_DIAGNOSTIC_MINIMAL_LOOP's
# definition in platform/stm32u031/CMakeLists.txt for what runs
# instead. Isolates whether a real, unmodified, as-normally-compiled
# platform_stm32u031.c reaches low Stop 2 current when driven directly
# by a trivial loop, as opposed to through vault_core -- a known-good
# reference firmware ("sleep3") reaches ~1.3-1.5uA on the same chip
# despite every individual piece of vault_core's real per-cycle
# sequence having been hand-ported to it and tested clean in
# isolation, so this test targets what a hand-port can't rule out:
# something in vault_core.c/vault_i2c_registers.c itself, or
# something in the build/linker/startup infrastructure a hand-copy
# into a different project could never capture. See git history for
# the Stop 2 current investigation.
option(VAULT_DIAGNOSTIC_MINIMAL_LOOP
    "Bypass vault_core entirely; drive platform_stm32u031.c directly from a trivial loop -- diagnostic A/B test only"
    OFF)
if(VAULT_DIAGNOSTIC_MINIMAL_LOOP)
    target_compile_definitions(vault_core PUBLIC VAULT_DIAGNOSTIC_MINIMAL_LOOP)
endif()

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
if(VAULT_TARGET STREQUAL "lpc810" OR VAULT_TARGET STREQUAL "stm32u031" OR VAULT_TARGET STREQUAL "stm32c011")
    target_compile_options(vault_core PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
elseif(VAULT_TARGET STREQUAL "efm32g210")
    # EFM32G210 is a Cortex-M3 part, genuinely different core than the
    # other two backends' Cortex-M0+.
    target_compile_options(vault_core PRIVATE -mcpu=cortex-m3 -mthumb -Os)
endif()
