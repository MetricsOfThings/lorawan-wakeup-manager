# LoRaWAN Wakeup Manager

Firmware for the "Data Vault" auxiliary MCU described in
`docs/MCU_Analysis_Report.md` and `docs/superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md`.

## Building

This project uses CMake with three targets, selected via `-DVAULT_TARGET`:

### Host (unit tests, no hardware required)

```bash
git submodule update --init --recursive
mkdir -p build/host && cd build/host
cmake ../.. -DVAULT_TARGET=host
cmake --build .
ctest --output-on-failure
```

(Note: the submodule update will fetch a few extra unused nested submodules from the STM32Cube package, which is expected and harmless.)

### LPC810 (real hardware, requires arm-none-eabi-gcc)

```bash
mkdir -p build/lpc810 && cd build/lpc810
cmake ../.. -DVAULT_TARGET=lpc810
cmake --build .
arm-none-eabi-objcopy -O binary platform/lpc810/vault_lpc810 vault_lpc810.bin
# flash vault_lpc810.bin via your ISP/SWD programmer
```

The ELF (and its `vault_lpc810.map` link map) land in `platform/lpc810/`
under the build directory, not directly in it, since `vault_lpc810` is
built by the nested `platform/lpc810/CMakeLists.txt`.

Before flashing to real hardware, read the verification checklist at the
top of "Phase B" in
`docs/superpowers/plans/2026-08-08-lorawan-wakeup-manager.md` — several
register values in the LPC810 backend are marked as needing cross-check
against the NXP UM10601 reference manual.

### STM32U031F8P6 (build-only until hardware arrives)

```bash
mkdir -p build/stm32u031 && cd build/stm32u031
cmake ../.. -DVAULT_TARGET=stm32u031
cmake --build .
```

This target compiles and links against the vendored STM32Cube HAL but has
not been flashed or validated on real silicon yet — see the design spec's
"out of scope" section.

## Debug logging

Add `-DVAULT_LOG_ENABLED=ON` to any target's `cmake` configure step to
enable application-level logging from `core/vault_core.c` (wake/sleep
transitions, context-valid state, the wake interval in use). Off by
default.

- **LPC810:** logging requires giving up the dedicated `RESET` pin —
  see the comment in `platform/lpc810/src/platform_lpc810_uart.c` for
  why. Output is on `PIO0_5`, 57600 8N1, TX-only.
- **STM32U031F8P6:** no trade-off needed (this part has pins to spare).
  Output is on `PA2` (USART2), 57600 8N1, TX-only.
- **Host:** logs to stderr, useful when debugging `vault_core` logic
  locally alongside the unit tests.

## Adding a new STM32 family backend later

1. `git submodule add https://github.com/STMicroelectronics/STM32Cube<Family>.git vendor/STM32Cube<Family>`
2. Add `platform/<family>/` implementing every function in `core/include/vault/platform.h`.
3. Add the target to the top-level `CMakeLists.txt`'s `VAULT_TARGET` dispatch.

`core/` never changes for this.
