# STM32C011J6M6 Backend — Design

## 1. Purpose and scope

Add a fourth Data Vault platform backend, `stm32c011`, targeting the
STMicroelectronics STM32C011J6M6 (Cortex-M0+, SO8N — 8-pin package) on a
bare/hand-wired chip (no dev board, no fixed schematic to reference). This
backend implements the same `vault/platform.h` HAL contract the `lpc810`,
`stm32u031`, and `efm32g210` backends already satisfy, reusing `core/`
unchanged. No changes to the I2C register-map protocol, wake-scheduling
model, or context storage size are needed — this is purely a new HAL
implementation for a fourth MCU family.

Real hardware (bare STM32C011J6M6 chip, own wiring) is in hand, so this
backend goes straight to hardware bring-up rather than staying build-only,
same as the LPC810 and EFM32G210 backends.

This is the most pin-constrained backend in the project: SO8N has only 8
pins total, including power and ground, versus dozens on the other three
targets. That constraint drives most of the design decisions below.

## 2. Hardware reference

No board schematic exists for this target — pins are assigned here, not
read off a board. All physical STM32C011J6M6 pins:

| Pin | Signal | Notes |
|---|---|---|
| 1 | `PB7` → `MAIN_RAIL_EN` | Default `SYSCFG_CFGR3` binding for this pin — plain GPIO output, no AF/remap needed. |
| 2 | `VDD` | |
| 3 | `VSS` | |
| 4 | `PF2-NRST` → `NRST` | Kept dedicated (not repurposed) — unlike the LPC810 backend's ISP-pin trade-off, this part has no equivalent always-available recovery mechanism to lean on if hardware reset is lost. |
| 5 | `PA9` → I2C1 SCL | Reached via `SYSCFG_CFGR3` pin-binding (select `PA11` identity for this physical pin) *then* a separate `SYSCFG_CFGR1` remap (`PA11`→`PA9`). Confirmed against the real datasheet: this physical pin's only `CFGR3` alternative to its `PA8` default is `PA11`, and `PA8` itself has no I2C alternate function. |
| 6 | `PA10` → I2C1 SDA | Always bound to `PA12` (no `CFGR3` choice for this pin) — reached via the same `SYSCFG_CFGR1` remap (`PA12`→`PA10`). |
| 7 | `PA13` → `SWDIO` | Fixed — this pin has no `CFGR3` alternative, and (confirmed against the real datasheet) **no USART TX alternate function at all**, only `USART2_RX` — it cannot be repurposed for debug UART TX (see §7's correction). |
| 8 | `PA14-BOOT0` → `SWCLK` | Default binding. Shared with debug UART TX (`USART2_TX`, AF1) when `VAULT_LOG_ENABLED` — see §7. Also carries `BOOT0`, sampled by the ROM bootloader independent of firmware GPIO state (see below). |

No crystal pins are allocated — see §5 for why (this package has none to
spare, and the clock strategy doesn't need one).

**Resolved during implementation (was an open question at spec time):**
this SO8N package's `PA14`/`SWCLK`/pin 8 also carries `BOOT0`
(`PA14-BOOT0` is literally its datasheet pin name), and the datasheet's
own "Boot modes" section confirms `BOOT0` is sampled by the ROM
bootloader at startup, before any firmware-configured GPIO state takes
effect. This means a hardware recovery path independent of SWD **does**
exist on this backend: even if a `VAULT_LOG_ENABLED` build has
repurposed `SWCLK` for UART TX and live SWD is unusable, strapping
`BOOT0` at power-up forces boot into the system memory bootloader
(USART1 or I2C1, per the datasheet), which can reflash the part
regardless of what the previous firmware did to any GPIO.

## 3. Library approach

Use STM32Cube **HAL** (not LL/bare registers) — matches the STM32U031
backend's abstraction level, per project decision to keep a consistent
pattern across STM32-family backends despite this part's smaller flash
budget (32 KB flash / 6 KB SRAM on the C011x6 variant this part number
implies). `VAULT_CONTEXT_SIZE` at the existing project default (320
bytes) remains trivial against 6 KB SRAM — no RAM pressure from this
choice.

Vendor tree: `vendor/STM32CubeC0/` (new submodule, alongside the existing
`vendor/STM32CubeU0/` and `vendor/Gecko_SDK/` — the top-level
`scripts/setup-vendor-submodules.sh` needs a new branch for it, matching
the plain `git submodule update --init --recursive` pattern already used
for `STM32CubeU0` — STM32CubeC0 is not expected to have the
Gecko_SDK-style multi-gigabyte sparse-checkout problem, but confirm repo
size before assuming that).

## 4. File structure

Mirrors `platform/stm32u031/`:

```
platform/stm32c011/
  CMakeLists.txt
  linker/stm32c011j6.ld
  src/
    startup_stm32c011.c           -- vector table/Reset_Handler
    main.c                        -- platform_init(), main loop
    platform_stm32c011.c          -- clock init, RTC wake timer, sleep entry, GPIO
    platform_stm32c011_i2c.c      -- I2C slave driver (HAL listen-mode, matching stm32u031's pattern)
    platform_stm32c011_uart.c     -- debug UART TX on the shared SWCLK pin (VAULT_LOG_ENABLED only)
```

Whether `MAIN_RAIL_EN`/bus-isolation GPIO logic gets its own file (as
`efm32g210` ended up doing) or stays folded into `platform_stm32c011.c`
(as `stm32u031` does) is a Task 1 file-structure decision, not fixed
here — follow whichever this part's actual HAL call pattern makes
cleanest.

## 5. Clock and wake strategy

- **Core clock: internal HSI** (typically 48 MHz on this family, exact
  default to confirm against the vendored header) — no external crystal.
  This is a deliberate fit with the pin budget (§2): SO8N has no spare
  pins for `OSC_IN`/`OSC_OUT`, so an internal-oscillator-only design is
  not just simpler but the *only* option here, unlike EFM32G210 where
  HFXO was a board-populated choice among alternatives.
- **RTC clock: LSI** (internal ~32 kHz RC), not LSE. Same reasoning as
  above (no crystal pins available) — and consistent with this project's
  own STM32U031 backend, which independently ended up on LSI after a
  real LSE/load-capacitor problem during that backend's bring-up (see
  that backend's own history). LSI's lower accuracy (~±5% typical,
  temperature-dependent) is an accepted trade-off for this design's
  wake-interval use case (minutes-to-hours periods, not
  timing-critical), matching the precedent already set.
- **RTC wake timer**: `platform_wakeup_timer_arm(seconds)` configures the
  RTC wakeup timer (or equivalent compare/alarm mechanism — exact HAL
  call, e.g. `HAL_RTCEx_SetWakeUpTimer_IT()`, to confirm against the
  vendored STM32CubeC0 HAL once sourced) to fire after `seconds`.
  `platform_wakeup_timer_clear()` clears the corresponding interrupt
  flag. Register/HAL-call names and the RTC's actual tick-rate-vs-max-
  interval tradeoff (same class of problem EFM32G210's 24-bit `COMP0`
  register forced a prescaler decision on) need confirming once the
  vendored header is available — not assumed here.
- **Sleep mode: Stop mode** — retains SRAM and register state, matching
  `vault_core`'s resume-in-place assumption (the same requirement that
  drove the LPC810 Power-down choice, the STM32U031 STOP2 choice, and
  the EFM32G210 EM2-not-EM3 choice). STM32C0's exact Stop-mode variant
  naming (a single "Stop mode," or a STOP0/STOP1-style split like some
  STM32G0 parts) needs confirming against the vendored HAL — do not
  assume the STM32U031 backend's exact `HAL_PWREx_EnterSTOP2Mode()` call
  carries over unchanged; STM32C0 is a different family from STM32U0
  despite superficial similarity.
- **Standby/Shutdown modes are rejected** for the same reason EM3 was
  rejected for EFM32G210: both wipe main SRAM (Shutdown additionally
  wipes backup registers, per this project's own `MCU_Analysis_Report.md`
  entry for this exact part), breaking the resume-in-place assumption
  `vault_core`'s WAKE_MAIN/BUS_ISOLATION/ARM_SLEEP cycle depends on.

## 6. I2C slave driver

`platform_i2c_slave_init(addr)` configures the I2C peripheral's SDA/SCL
pins (open-drain, correct alternate function — exact pin/AF values to
confirm against the vendored device header, not guessed) and starts HAL
listen-mode slave reception, matching the STM32U031 backend's existing
pattern (`HAL_I2C_EnableListen_IT()` plus the `HAL_I2C_AddrCallback`/
`HAL_I2C_SlaveRxCpltCallback`/`HAL_I2C_SlaveTxCpltCallback`/
`HAL_I2C_ListenCpltCallback` callback set) rather than inventing a new
driver style — same peripheral family generation, same HAL I2C module
shape expected. `I2Cx_EV_IRQHandler`/`I2Cx_ER_IRQHandler` wiring and the
callbacks call the existing `vault_i2c_registers_on_write_byte()` /
`_on_read_request()` / `_on_stop()` hooks unchanged.

Bus speed: match the existing 400 kHz Fast-mode target already
established for all three other backends, once real hardware timing can
be verified.

### 6.1 Idle power while waiting for I2C

Implements `platform_wait_for_interrupt()` as a plain `__WFI()` call
(matching all three existing backends), called once per `WAKE_MAIN` wait
loop iteration instead of busy-spinning. `platform_irq_disable()`/
`_enable()` implement the standard `__disable_irq()`/`__enable_irq()`
pair, closing the same lost-wakeup race already fixed across every other
backend (see `vault/platform.h`'s doc comment) — this is not new design,
just confirming this backend follows the established, already-fixed
pattern from day one rather than needing its own later fix-round.

## 7. Debug UART on the shared SWCLK pin

USART TX-only, 57600 8N1 — matching the fixed baud rate established by
all three other backends (`VAULT_LOG_ENABLED` gate, `vault_log()`
contract from `core/vault_log.h`). RX intentionally unimplemented,
matching the TX-only pattern every other backend already uses.

**The distinctive part of this backend:** with only 8 physical pins and
every other signal already committed (§2), debug logging can only exist
by repurposing a debug pin as USART TX. **This spec originally proposed
`SWDIO` for this — that was wrong.** Checked against the real
STM32C011x4/x6 datasheet (DS13866) during implementation: `PA13`
(`SWDIO`) has no USART TX alternate function at all, only
`USART2_RX`. The pin that actually works is `PA14` (`SWCLK`), whose
AF1 is `USART2_TX` (AF0, the reset default, is `SWCLK` itself).
Mechanism, corrected to the real pin:

- `platform_stm32c011_uart.c`'s init function (called from
  `platform_init()`, `VAULT_LOG_ENABLED`-gated same as every other
  backend's UART init) reconfigures `PA14` from its default `SWCLK`
  alternate function (AF0) to `USART2_TX` (AF1, `GPIO_AF1_USART2`).
- From that point until the next power cycle/reset, **live SWD
  debugging of this chip is not possible** — SWD requires both
  `SWDIO` and `SWCLK`, so losing either breaks it equally; the
  consequence is unchanged from the original (wrong-pin) plan, just the
  specific pin differs. This is the same category of trade-off the
  LPC810 backend already makes by sacrificing its dedicated `NRST` pin
  for debug logging; here it costs `SWCLK` instead of `NRST`.
- Recovery: reflash a `VAULT_LOG_ENABLED=OFF` build (which never touches
  `SWCLK`'s SWD function) over SWD *before* the log-enabled build's
  `platform_init()` runs — i.e. flash while the chip is freshly reset
  and still running the previous (or blank) firmware, not after the
  log-enabled build has already reconfigured the pin.
- **Second, more robust recovery path, confirmed during implementation
  (was open at spec time):** `PA14` also carries `BOOT0`
  (`PA14-BOOT0` is its literal datasheet pin name), sampled by the ROM
  bootloader at power-up independent of whatever the previous firmware
  did to the pin's GPIO/AF state. Strapping `BOOT0` forces boot into
  the system memory bootloader (USART1 or I2C1, per the datasheet's
  Boot modes section), which can reflash the part even if
  `VAULT_LOG_ENABLED`'s SWCLK repurposing has made live SWD unusable.
- A build-time note (`CMakeLists.txt`/README) must state this trade-off
  explicitly, mirroring how the LPC810 backend's own README section
  already documents its RESET-pin trade-off — a silent SWD-loss surprise
  during bring-up would be a bad experience.

## 8. Build integration

- `VAULT_TARGET=stm32c011` added to the top-level `CMakeLists.txt`'s
  target dispatch, alongside `host`/`lpc810`/`stm32u031`/`efm32g210`.
- `VAULT_CONTEXT_SIZE` default: 320 bytes, matching all three existing
  hardware backends (same RadioLib-driven requirement; trivial against
  this part's 6 KB SRAM — the tightest RAM budget of any backend so far,
  but still comfortably sufficient).
- `VAULT_LOG_ENABLED` opt-in pattern reused unchanged — conditionally
  compiles `platform_stm32c011_uart.c` and defines `VAULT_LOG_ENABLED`,
  same as all three other backends' `CMakeLists.txt` files already do.
  Given §7's trade-off, this flag now controls not just "extra logging
  code" but "whether SWD stays usable at runtime" — worth a comment at
  the point this flag is defined, not just in the README.
- `vault_core`'s existing `-mcpu`/`-mthumb`/`-Os` compile-option keying
  (`core/CMakeLists.txt`, keyed off `VAULT_TARGET`) currently has one
  branch shared by `lpc810`/`stm32u031` (`-mcpu=cortex-m0plus`, both
  Cortex-M0+) and a separate branch for `efm32g210`
  (`-mcpu=cortex-m3`). STM32C011J6M6 is also Cortex-M0+, so `stm32c011`
  joins the existing `lpc810`/`stm32u031` branch rather than needing a
  new one — confirm this against the real vendored device header before
  assuming it (Cortex-M0 vs. M0+ has bitten this project before).
- `scripts/setup-vendor-submodules.sh` needs a new `vendor/STM32CubeC0`
  branch (§3).

## 9. Open questions / verify during implementation

Consistent with this project's established practice of flagging
unverified register-level details rather than presenting invented
confidence. Updated post-implementation: the real STM32C011x4/x6
datasheet (DS13866) became available mid-implementation and resolved
every item below except where noted.

**Resolved:**

- ~~Exact SO8N pin-to-signal map.~~ Resolved against DS13866's Table 12
  "Pin assignment and description": see §2's pin table for the real,
  final assignment. This also caught and corrected two real errors an
  earlier (pre-datasheet) implementation task had made using
  provisional pins: I2C SDA/SCL were originally assigned to `PF2`/`PA8`
  (neither has any I2C alternate function at all) before being
  corrected to `PA9`/`PA10`; debug UART TX was originally planned for
  `SWDIO` (`PA13`, no USART TX capability) before being corrected to
  `SWCLK` (`PA14`, which does have one). Both are recorded in this
  repo's `.superpowers/sdd/progress.md` ledger with the fix commits.
- ~~BOOT0/system-bootloader accessibility on SO8N~~, per §2 — resolved
  yes: `PA14`/`SWCLK`/pin 8 also carries `BOOT0`, confirmed sampled by
  the ROM bootloader independent of firmware GPIO state. See §7.
- **I2C peripheral instance and its HAL listen-mode behavior on this
  specific part.** STM32U031's I2C driver is the closest precedent, but
  confirm STM32C0's I2C peripheral generation/HAL module actually
  matches closely enough to reuse that pattern directly rather than
  needing its own adaptation.
- **RTC wakeup timer register/HAL-call names and max-interval ceiling**
  on STM32C0 specifically — do not assume STM32U031's exact call names
  or tick-rate math carry over unchanged (see §5).
- **Stop-mode exact variant and entry/exit HAL call** — do not assume
  `HAL_PWREx_EnterSTOP2Mode()` (STM32U031's call) exists unchanged on
  STM32C0; confirm the real API surface once the HAL is vendored.
- **Default HSI frequency and whether any startup calibration/trim step
  is needed** before it's trustworthy as the USART baud-rate reference
  clock (the same class of "stale clock global" issue Task 4/7 already
  hit and fixed on the EFM32G210 backend).
