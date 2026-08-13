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
| 1 | `VDD` | |
| 2 | `VSS` | |
| 3 | `NRST` | Kept dedicated (not repurposed) — unlike the LPC810 backend's ISP-pin trade-off, this part has no equivalent always-available recovery mechanism to lean on if hardware reset is lost. |
| 4 | `MAIN_RAIL_EN` | GPIO output — exact pin name (e.g. `PA4`) to be confirmed against the real STM32C011 SO8N pinout table once vendored (see §9). |
| 5 | I2C SDA | Exact pin/AF to be confirmed against the vendored device header. |
| 6 | I2C SCL | Exact pin/AF to be confirmed against the vendored device header. |
| 7 | `SWDIO` | Shared with debug UART TX when `VAULT_LOG_ENABLED` — see §7. |
| 8 | `SWCLK` | |

No crystal pins are allocated — see §5 for why (this package has none to
spare, and the clock strategy doesn't need one).

**Open, safety-relevant question this spec cannot resolve without the
real datasheet in hand:** whether this SO8N package exposes any way to
force entry into ST's ROM system bootloader (normally a `BOOT0` pin
strap) independent of what user code has done to `SWDIO`/`SWCLK` at
runtime. This is the recovery path if a `VAULT_LOG_ENABLED` build (which
repurposes `SWDIO`, see §7) ever needs to be reflashed and live SWD is
unusable. Flagged for confirmation during implementation, not assumed
resolved by this spec.

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
    platform_stm32c011_uart.c     -- debug UART TX on the shared SWDIO pin (VAULT_LOG_ENABLED only)
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

## 7. Debug UART on the shared SWDIO pin

USART TX-only, 57600 8N1 — matching the fixed baud rate established by
all three other backends (`VAULT_LOG_ENABLED` gate, `vault_log()`
contract from `core/vault_log.h`). RX intentionally unimplemented,
matching the TX-only pattern every other backend already uses.

**The distinctive part of this backend:** with only 8 physical pins and
every other signal already committed (§2), debug logging can only exist
by repurposing `SWDIO` as USART TX. Mechanism:

- `platform_stm32c011_uart.c`'s init function (called from
  `platform_init()`, `VAULT_LOG_ENABLED`-gated same as every other
  backend's UART init) reconfigures the `SWDIO` pin from its default SWD
  alternate function to the USART TX alternate function.
- From that point until the next power cycle/reset, **live SWD
  debugging of this chip is not possible** — the debug probe cannot talk
  to a pin now driving UART data. This is the same category of trade-off
  the LPC810 backend already makes by sacrificing its dedicated `NRST`
  pin for debug logging; here it costs `SWDIO` instead of `NRST`.
- Recovery: reflash a `VAULT_LOG_ENABLED=OFF` build (which never touches
  `SWDIO`'s SWD function) over SWD *before* the log-enabled build's
  `platform_init()` runs — i.e. flash while the chip is freshly reset
  and still running the previous (or blank) firmware, not after the
  log-enabled build has already reconfigured the pin. If this part's
  ROM system bootloader is reachable independent of user-code GPIO state
  (§2's open question), that provides a second, more robust recovery
  path; confirm during implementation before relying on it.
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
confidence — this backend has more open items than usual at spec time
because there is no existing board schematic to read pin assignments
from; they get finalized against the real vendored device header/
datasheet during implementation, not guessed here.

- **Exact SO8N pin-to-signal map.** Which physical pin numbers correspond
  to which GPIO names (e.g. `PA4`, `PA9`, `PA13`), and which of those
  have I2C/USART alternate-function options — needs the real STM32C011
  datasheet pinout table and vendored device header, not assumption.
  `MAIN_RAIL_EN`'s GPIO choice in particular is only listed by role in
  §2, not by real pin name, until this is resolved.
- **BOOT0/system-bootloader accessibility on SO8N**, per §2 — determines
  whether there's a hardware recovery path independent of §7's SWDIO
  trade-off.
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
