# LoRaWAN Wakeup Manager

Firmware for the "Data Vault" auxiliary MCU described in
`docs/MCU_Analysis_Report.md` and `docs/superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md`.

## Building

This project uses CMake with five targets, selected via `-DVAULT_TARGET`:

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

STM32U031F8P6 is a 20-pin TSSOP20 package (64 KB flash / 12 KB SRAM).
Full pinout (from the real STM32U031x4/6/8 datasheet, DS14581 Rev 2,
Table 12 "STM32U031x4/6/8 pin/ball definition" and Figure 3 "TSSOP20
pinout", both read directly off the vendor PDF, not derived):

| Pin | Name (function after reset) | Used by this backend |
| --- | --- | --- |
| 1 | `PB8`/`PB9` | — |
| 2 | `VSS` | Ground |
| 3 | `VDD` | Power |
| 4 | `PC14-OSC32_IN` | — |
| 5 | `PC15-OSC32_OUT` | — |
| 6 | `PF2-NRST` | `NRST` |
| 7 | `VDDA`/`VREF+` | Tied to `VDD` |
| 8 | `PA0-CK_IN` | `MAIN_RAIL_EN` (`PA0`) |
| 9 | `PA1`/`PA2` | Debug UART TX (`PA2`, `USART2`) |
| 10 | `PA3`/`PA4` | — |
| 11 | `PA5`/`PA6` | — |
| 12 | `PA7`/`PB0` | — |
| 13 | `PB1` | — |
| 14 | `PA8`/`PA9`/`PA10` | I2C1 SCL/SDA (`PA9`/`PA10`) — **see caveat below** |
| 15 | `PA11` (remappable to `PA9` via `SYSCFG_CFGR1`) | Alternate route to `PA9` — **not used by current code** |
| 16 | `PA12` (remappable to `PA10` via `SYSCFG_CFGR1`) | Alternate route to `PA10` — **not used by current code** |
| 17 | `PA13` (`SWDIO`) | `SWDIO` |
| 18 | `PA14` (`SWCLK`)/`PB4`/`PB5`/`PB6` | `SWCLK` |
| 19 | `PB7` | — |
| 20 | `PF3-BOOT0` (`BOOT0`) | — |

Like the STM32C011J6M6's SO8N package (see below), this reduced-pin
TSSOP20 package multiplexes several GPIO identities onto single physical
leads: pin 14 alone can present as `PA8`, `PA9`, *or* `PA10` depending on
a `SYSCFG` pin-binding selection, and the datasheet separately documents
that "`PA9`/`PA10` can be remapped in place of pins `PA11`/`PA12`
(default mapping), using `SYSCFG_CFGR1` register" — i.e. pins 15/16
default to `PA11`/`PA12` and only show `PA9`/`PA10` if explicitly
remapped.

**Caveat:** [`platform_stm32u031.c`](platform/stm32u031/src/platform_stm32u031.c)
configures I2C1 as `GPIOA` pin 9/10 (`I2C_SCL_PIN`/`I2C_SDA_PIN`) but
makes no `SYSCFG` binding or remap call at all before doing so. Whether
that reaches a physically functional pin depends on pin 14's power-on
default binding (undocumented in this datasheet — the STM32C011J6M6
backend needed an explicit call for the equivalent case, see
"STM32C011 Alarm-A" and "SYSCFG_CFGR3" fixes in git history) or, if not,
whether the fallback should instead be an explicit `SYSCFG_CFGR1` remap
onto pins 15/16. This has not been exercised on real hardware yet (this
target is still build-only) — verify I2C1 actually works during this
backend's own hardware bring-up before trusting it silently.

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
| `MAIN_RAIL_EN` | `PC13` | Free GPIO, broken out on `CON2` pin 5 |
| I2C0 SDA | `PD6` | `CON1` pin 7 / `UEXT` pin 6 |
| I2C0 SCL | `PD7` | `CON1` pin 8 / `UEXT` pin 5 |
| Debug UART TX (USART1) | `PC0` | `CON1` pin 3 |
| LFXO (32.768 kHz) | `PB7`/`PB8` | Already populated on-board (Q1) — RTC wake-timer clock source |
| HFXO (32 MHz) | `PB13`/`PB14` | Already populated on-board (Q2) — main clock source |
| `RSTN` | dedicated `#RESET` pin | `DBG` connector pin 15 / `CON2` pin 2 |

Already-committed pins not repurposed by this backend: `PA0` (on-board
status LED), `PA1` (on-board user button), `#RESET` (reset button).

### STM32C011J6M6 (real hardware in hand, flashed successfully)

```bash
./scripts/setup-vendor-submodules.sh
mkdir -p build/stm32c011 && cd build/stm32c011
cmake ../.. -DVAULT_TARGET=stm32c011
cmake --build .
```

This target compiles and links against the vendored STM32Cube C0 HAL. No
board schematic exists for this part — it's a bare, hand-wired chip
rather than a dev board, so unlike the other backends the pin table below
is assigned here, not read off a board schematic (see the design spec's
§1). Real hardware for this backend has been flashed successfully over
SWD (see the "Flashing with OpenOCD" section below for the exact command
and the decoupling-capacitor gotcha that blocked the first attempts).

**Bring-up note (hand-wired chip, no decoupling cap):** the first several
SWD connect attempts on real hardware failed with `Error connecting DP:
cannot read IDR`, even with power/wiring/continuity all individually
confirmed correct and a known-good probe/cable. Root cause was a missing
decoupling capacitor: with only bare VDD/VSS leads (no local bypass
capacitance), DC voltage measures clean and steady on a multimeter, but
the transient current draw from SWD's own clock toggling was enough to
disturb VDD locally and destabilize the debug port. A 100 nF ceramic
capacitor soldered directly across VDD (pin 2) and VSS (pin 3), as close
to the package as possible, resolved it. If you hit the same
`cannot read IDR` error on a hand-wired build of this backend, check for
a decoupling cap before suspecting the chip itself.

STM32C011J6M6 is an 8-pin SO8N package (32 KB flash / 6 KB SRAM), so
every pin is already committed — there are no pins to spare. Pin
assignments (from
`docs/superpowers/specs/2026-08-13-stm32c011-backend-design.md` §2,
confirmed against the real STM32C011x4/x6 datasheet, DS13866):

| Pin | Signal | Notes |
| --- | --- | --- |
| 1 | `PB7` → `MAIN_RAIL_EN` | Default `SYSCFG_CFGR3` binding — plain GPIO output, no remap needed. |
| 2 | `VDD` | |
| 3 | `VSS` | |
| 4 | `PF2-NRST` → `NRST` | Kept dedicated — this part has no LPC810-style always-available recovery pin to fall back on if hardware reset is lost. |
| 5 | `PA9` → I2C1 SCL | Via `SYSCFG_CFGR3` pin-binding (`PA11` identity for this physical pin), then a separate `SYSCFG_CFGR1` remap (`PA11`→`PA9`). |
| 6 | `PA10` → I2C1 SDA | Via `SYSCFG_CFGR1` remap (`PA12`→`PA10`) only — this pin has no `CFGR3` alternative. |
| 7 | `PA13` → `SWDIO` | Fixed, no alternative binding. **Has no USART TX alternate function at all** (only `USART2_RX`) — cannot be repurposed for debug logging. |
| 8 | `PA14-BOOT0` → `SWCLK` | Default binding. Repurposed as `USART2_TX` (AF1) when `VAULT_LOG_ENABLED` — see "Debug logging" below. Also carries `BOOT0`, a hardware recovery path independent of SWD (see below). |

**Debug-logging trade-off (read before flashing a `VAULT_LOG_ENABLED`
build):** with every pin already committed, this backend can only get
debug UART by repurposing `SWCLK` (pin 8) as `USART2_TX`. From the
moment `platform_init()` runs in such a build, **live SWD debugging of
this chip is impossible** — SWD needs both `SWDIO` and `SWCLK`, and this
build has reassigned the latter to UART TX. This is the same category of
trade-off the LPC810 section above documents for its dedicated `RESET`
pin, just costing `SWCLK` here instead. Two recovery paths if you get
stuck:

1. Reflash a `VAULT_LOG_ENABLED=OFF` build over SWD immediately after a
   power cycle/reset, before the log-enabled firmware's `platform_init()`
   has a chance to run and reclaim the pin.
2. Strap `BOOT0` (also carried on pin 8, `PA14-BOOT0`) at power-up to
   force the ROM bootloader (USART1 or I2C1) regardless of what the
   previous firmware did to the pin's GPIO/AF state — this works even if
   SWD is fully unusable.

## Pin reference

Quick lookup for the pins of interest across all four hardware backends —
where `SWDIO`/`SWCLK` (debug probe), `MAIN_RAIL_EN`, and the debug UART TX
pin actually are. See each backend's own subsection above/below for the
full pin table and sourcing (board schematic vs. hand-assigned) —
this table only pulls out the four signals most likely to matter when
wiring up a probe or a rail-switch.

| Backend | `SWDIO` | `SWCLK` | `MAIN_RAIL_EN` | Debug UART TX |
| --- | --- | --- | --- | --- |
| LPC810 | `PIO0_2` (fixed) | `PIO0_3` (fixed) | `PIO0_0` | `PIO0_5` — shares the dedicated `RESET` pin; only present when `VAULT_LOG_ENABLED` (see below) |
| STM32U031F8P6 | `PA13` (fixed) | `PA14` (fixed) | `PA0` | `PA2` (USART2) — no trade-off, this part has pins to spare |
| EFM32G210F128 | Olimex `DBG` connector (chip pin not separately assigned/verified — not repurposed by this backend, so it was never needed) | Olimex `DBG` connector (same) | `PC13` | `PC0` (USART1) — no trade-off, this part has pins to spare |
| STM32C011J6M6 | `PA13` (pin 7, fixed — **cannot** carry UART, no USART TX alternate function exists on this pin) | `PA14` (pin 8, default) | `PB7` (pin 1) | **`PA14`/`SWCLK` itself** (pin 8, `USART2_TX` AF1) — only present when `VAULT_LOG_ENABLED`, and doing so makes live SWD debugging unavailable for the life of that build (see the STM32C011J6M6 build section above) |

The STM32C011J6M6 row is the one to read carefully before flashing a
`VAULT_LOG_ENABLED` build: its debug UART TX pin *is* `SWCLK`, not a
separate free pin like every other backend has.

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
- **STM32C011J6M6:** repurposes the `SWCLK` pin (not `SWDIO`) as
  `USART2_TX`, 57600 8N1, TX-only — **this makes live SWD debugging
  unavailable for the lifetime of the running build**. See the
  "Debug-logging trade-off" note under the STM32C011J6M6 build section
  above for the mechanism and the two recovery paths (reflash a
  logging-disabled build before `platform_init()` runs, or strap
  `BOOT0`, also carried on this pin, to force the ROM bootloader).
- **Host:** logs to stderr, useful when debugging `vault_core` logic
  locally alongside the unit tests.

## Flashing with OpenOCD

Each target links to an ELF, not a raw binary, so `openocd`'s `program`
command (which handles the ELF's load addresses itself) is the simplest
flashing path — no `objcopy` step needed. Run these from the target's
build directory after `cmake --build .`. `verify reset exit` flashes,
verifies against flash, resets the MCU into the new firmware, and exits
OpenOCD.

All probes below assume a standard 10-pin/20-pin ARM SWD connection
(`SWDIO`/`SWCLK`/`GND`/`VTref`, `RESET` optional but recommended).

### LPC810

```bash
openocd -f interface/cmsis-dap.cfg -f target/lpc8xx.cfg \
  -c "program platform/lpc810/vault_lpc810 verify reset exit"
```

- Swap `interface/cmsis-dap.cfg` for whatever probe you actually have
  (`interface/stlink-dap.cfg`, `interface/jlink.cfg`, etc.) — LPC810 only
  needs generic Cortex-M0+/SWD support, so any OpenOCD-supported SWD
  probe works, this isn't NXP-specific.
- `target/lpc8xx.cfg` ships in stock OpenOCD (confirmed present in the
  Homebrew 0.12.0 package) — no vendor fork required.
- Flash is mapped at `0x00000000` (`platform/lpc810/linker/lpc810.ld`).

### STM32U031F8P6

```bash
openocd -f interface/stlink-dap.cfg -f target/stm32u0x.cfg \
  -c "program platform/stm32u031/vault_stm32u031 verify reset exit"
```

- **Stock OpenOCD 0.12.0 (e.g. the Homebrew package) does not ship
  `target/stm32u0x.cfg`** — STM32U0 support landed after that release.
  Use either:
  - ST's own OpenOCD build and scripts, bundled with STM32CubeIDE, e.g.
    on macOS:
    `.../STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.openocd.<arch>_*/tools/bin/openocd`
    with `-s .../com.st.stm32cube.ide.mcu.debug.openocd_*/resources/openocd/st_scripts`, or
  - a mainline OpenOCD built from a recent enough source/nightly that
    includes `target/stm32u0x.cfg`.
- This target has not been flashed on real silicon yet (see the README
  section above) — treat the command as untested until hardware arrives.
- Flash is mapped at `0x08000000`
  (`platform/stm32u031/linker/stm32u031f8.ld`).

### EFM32G210F128

```bash
openocd -f interface/cmsis-dap.cfg -f target/efm32.cfg -c "adapter speed 400" \
  -c "program platform/efm32g210/vault_efm32g210 verify reset exit"
```

- `target/efm32.cfg` is generic across all EFM32 families, including
  this Series-0 "Gecko" part, and ships in stock OpenOCD.
- The Olimex EM-32G210F128-H has no on-board debugger — connect an
  external SWD probe to its `DBG` connector (`SWDIO`/`SWCLK`/`RSTN`, see
  the pin table above for `RSTN`'s connector pin).
- Flash is mapped at `0x00000000`
  (`platform/efm32g210/linker/efm32g210f128.ld`).
- **`adapter speed 400` is required** — `efm32.cfg`'s default of
  1000 kHz is too fast for a hand-wired external SWD connection on this
  board and reliably fails partway through flash erase (`Failed to read
  memory at 0x400c0020`, then every subsequent memory access fails too).
  Confirmed on real hardware: dropping to 400 kHz fixed it. If 400 still
  fails, try lower (e.g. 100) before suspecting a hardware/wiring fault.

### STM32C011J6M6

```bash
openocd -f interface/stlink-dap.cfg -f target/stm32c0x.cfg \
  -c "program platform/stm32c011/vault_stm32c011 verify reset exit"
```

- **Stock OpenOCD 0.12.0 (the Homebrew package) does not ship
  `target/stm32c0x.cfg`** — confirmed by checking the installed package's
  `scripts/target/` directory (`find $(brew --prefix openocd)/share/openocd/scripts/target -iname 'stm32c0*.cfg'` — no match; `stm32g0x.cfg` is the
  nearest sibling family present, but that is a different chip family and
  not a substitute). This is the same gap the STM32U031F8P6 section above
  documents for `target/stm32u0x.cfg`. Use either:
  - ST's own OpenOCD build and scripts, bundled with STM32CubeIDE (see
    the STM32U031F8P6 section above for the macOS path), or
  - a mainline OpenOCD built from a recent enough source/nightly that
    includes `target/stm32c0x.cfg`.
- This target has no board — it's a bare, hand-wired chip — but has been
  flashed and confirmed working on real silicon (see the build section
  above).
- Flash is mapped at `0x08000000`
  (`platform/stm32c011/linker/stm32c011j6.ld`).
- If SWD debugging becomes unresponsive on a `VAULT_LOG_ENABLED` build,
  see the "Debug-logging trade-off" note in the build section above
  before assuming a wiring fault — `SWCLK` may simply be running as
  UART TX.
- If you get `Error connecting DP: cannot read IDR` on a fresh hand-wired
  build of this chip, see the "Bring-up note" in the build section above
  — a missing VDD/VSS decoupling capacitor is the most likely cause, not
  a wiring fault.

## Adding a new STM32 family backend later

1. `git submodule add https://github.com/STMicroelectronics/STM32Cube<Family>.git vendor/STM32Cube<Family>`
2. Add `platform/<family>/` implementing every function in `core/include/vault/platform.h`.
3. Add the target to the top-level `CMakeLists.txt`'s `VAULT_TARGET` dispatch.

`core/` never changes for this.
