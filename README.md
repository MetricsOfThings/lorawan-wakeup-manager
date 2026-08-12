# LoRaWAN Wakeup Manager

Firmware for the "Data Vault" auxiliary MCU described in
`docs/MCU_Analysis_Report.md` and `docs/superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md`.

## Building

This project uses CMake with four targets, selected via `-DVAULT_TARGET`:

### First-time setup: vendor submodules

Before building any target, run:

```bash
./scripts/setup-vendor-submodules.sh
```

This initializes `vendor/CMSIS_5`, `vendor/STM32CubeU0`, and
`vendor/Gecko_SDK` and is safe to re-run at any time. It matters
specifically for `vendor/Gecko_SDK`: that submodule is a clone of
Silicon Labs' Gecko SDK, a multi-gigabyte monorepo bundling several
unrelated protocol stacks (Bluetooth, Zigbee, Thread, Matter, etc.).
This repo's EFM32G210 backend only needs four of its subdirectories,
so the script applies a `git sparse-checkout` to fetch just those —
git does not record sparse-checkout scoping anywhere in the tracked
repo (`.gitmodules` can't express it), so a plain
`git submodule update --init --recursive` would check out the entire
SDK instead and can exhaust disk space on a constrained host (this
happened once during development; see
`.superpowers/sdd/task-1-report.md`). Always use the script rather
than a raw `git submodule update` for this repo.

### Host (unit tests, no hardware required)

```bash
./scripts/setup-vendor-submodules.sh
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

### EFM32G210F128 (real hardware in hand, pending bring-up verification)

```bash
./scripts/setup-vendor-submodules.sh
mkdir -p build/efm32g210 && cd build/efm32g210
cmake ../.. -DVAULT_TARGET=efm32g210
cmake --build .
```

Board: Olimex EM-32G210F128-H. This target compiles and links against the
vendored Silicon Labs Gecko SDK (CMSIS + a handful of `emlib` source
files — `em_gpio.c`, `em_cmu.c`, `em_core.c`, `em_rtc.c`, `em_emu.c`, and,
with logging enabled, `em_usart.c` — pulled in because this part declares
those functions non-inline; see the comments in
`platform/efm32g210/CMakeLists.txt` for how each was confirmed necessary).
Unlike the STM32U031F8P6 target above, real Olimex hardware for this
backend is already in hand (see the design spec's §1) — it has not yet
been flashed or validated on real silicon, but that's tracked as a
concrete next step (Task 9, manual hardware bring-up verification), not
an indefinite "until hardware arrives" wait.

Pin assignments (from the board schematic, see
`docs/superpowers/specs/2026-08-12-efm32g210-backend-design.md` §2):

| Signal | Pin | Notes |
| --- | --- | --- |
| `MAIN_RAIL_EN` | `PC13` | Free GPIO, broken out on `CON2` pin 4 |
| I2C0 SDA | `PD6` | `CON1` pin 7 / `UEXT` pin 6 |
| I2C0 SCL | `PD7` | `CON1` pin 8 / `UEXT` pin 5 |
| Debug UART TX (USART1) | `PC0` | `CON1` pin 4 |
| LFXO (32.768 kHz) | `PB7`/`PB8` | Already populated on-board (Q1) — RTC wake-timer clock source |
| HFXO (32 MHz) | `PB13`/`PB14` | Already populated on-board (Q2) — main clock source |
| `RSTN` | dedicated `#RESET` pin | `DBG` connector pin 15 / `CON2` pin 2 |

Already-committed pins not repurposed by this backend: `PA0` (on-board
status LED), `PA1` (on-board user button), `#RESET` (reset button).

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
- **EFM32G210F128:** no trade-off needed (this part also has pins to
  spare). Output is on `PC0` (USART1), 57600 8N1, TX-only; `PC1` (RX)
  is left unconfigured.
- **Host:** logs to stderr, useful when debugging `vault_core` logic
  locally alongside the unit tests.

## Adding a new STM32 family backend later

1. `git submodule add https://github.com/STMicroelectronics/STM32Cube<Family>.git vendor/STM32Cube<Family>`
2. Add `platform/<family>/` implementing every function in `core/include/vault/platform.h`.
3. Add the target to the top-level `CMakeLists.txt`'s `VAULT_TARGET` dispatch.

`core/` never changes for this.
