# EFM32G210F128 Backend — Design

## 1. Purpose and scope

Add a third Data Vault platform backend, `efm32g210`, targeting the Silicon
Labs EFM32G210F128 (Cortex-M3, QFN32) on the Olimex EM-32G210F128-H board.
This backend implements the same `vault/platform.h` HAL contract the
`lpc810` and `stm32u031` backends already satisfy, reusing `core/`
unchanged. No changes to the I2C register-map protocol, wake-scheduling
model, or context storage size are needed — this is purely a new HAL
implementation for a third MCU family.

Unlike the STM32U031 backend, real Olimex hardware is in hand, and this
backend goes straight to hardware bring-up rather than staying build-only.

## 2. Hardware reference

Board: Olimex EM-32G210F128-H (schematic/pinout confirmed from the
board's own user manual, not assumed):

| Signal | Chip pin | Notes |
|---|---|---|
| `MAIN_RAIL_EN` | `PC13` | Free GPIO, broken out on `CON2` pin 4, not committed to any on-board fixed function |
| I2C0 SDA | `PD6` | Board schematic labels this `I2C0_SDA`; broken out on `CON1` pin 7 and `UEXT` pin 6 |
| I2C0 SCL | `PD7` | Board schematic labels this `I2C0_SCL`; broken out on `CON1` pin 8 and `UEXT` pin 5 |
| Debug UART TX | `PC0` | USART1 TX (`US1_TX` on schematic), broken out on `CON1` pin 4 |
| LFXO (32.768 kHz) | `PB7`/`PB8` | Already populated on-board (Q1, with its own load caps) — this is the RTC wake-timer clock source |
| HFXO (32 MHz) | `PB13`/`PB14` | Already populated on-board (Q2) — main clock source |
| `RSTN` | dedicated `#RESET` pin | Available on `DBG` connector pin 15 and `CON2` pin 2 — unlike the LPC810 backend, hardware reset can be wired if useful for debugging |

Already-committed pins we must not repurpose: `PA0` (on-board status LED),
`PA1` (on-board user button, via `BUT_E` jumper), `#RESET` (reset button
circuit).

`I2C0` on `PD6`/`PD7` and `USART1` on `PC0`/`PC1` are EFM32's fixed
"peripheral I/O location" routing choices for this specific pin pair —
verify the exact `LOCATION` field value in `I2C0->ROUTE` /
`USART1->ROUTE` against the reference manual and vendored `efm32g210f128.h`
once sourced (see Open Questions).

## 3. Library approach

Use Silicon Labs' **emlib** (the vendor peripheral library), not bare
CMSIS register pokes — matching the abstraction level of the STM32U031
backend's HAL usage rather than the LPC810 backend's raw-register style.
Rationale (per project discussion): emlib is well-documented and reduces
the risk of the class of subtle register-level mistakes this project hit
repeatedly during STM32 bring-up (wrong AF numbers, missing wake-source
enables, etc.).

Vendor tree: `vendor/EFM32G210_SDK/` (or equivalent — exact name depends
on what Silicon Labs' Gecko SDK download actually calls itself), containing
the CMSIS device header (`efm32g210f128.h`), startup file, and the emlib
sources actually needed: `em_cmu.c` (clock management), `em_gpio.c`,
`em_i2c.c`, `em_usart.c`, `em_rtc.c`, `em_emu.c` (energy modes /
`EMU_EnterEM2()`), plus whatever `em_core.c`/`em_system.c` support files
those depend on. Confirm the exact file list once the SDK is actually
vendored — don't guess a file that doesn't build.

## 4. File structure

Mirrors `platform/stm32u031/`:

```
platform/efm32g210/
  CMakeLists.txt
  linker/efm32g210f128.ld
  src/
    main.c                       -- SystemClock/CMU init, platform_init(), main loop
    platform_efm32g210.c         -- GPIO (main rail, bus isolate), RTC wake timer, sleep entry
    platform_efm32g210_i2c.c     -- I2C0 slave driver
    platform_efm32g210_uart.c    -- USART1 TX-only debug log (VAULT_LOG_ENABLED only)
```

## 5. Clock and wake strategy

- **HFXO** (32 MHz, already populated) as the main/core clock source,
  selected via `CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO)` (verify
  exact emlib call against the vendored header).
- **LFXO** (32.768 kHz, already populated) as the clock source for the
  RTC peripheral, via `CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO)`.
  Going straight to LFXO here (not staging through the internal LFRCO
  first) is a deliberate difference from the STM32U031 backend's
  LSE-vs-LSI saga: this crystal is already populated and presumably
  validated by Olimex's own demo firmware, so there's less reason to
  expect the same missing-load-cap problem.
- **RTC** (24-bit, per the board's block diagram) drives the wake timer:
  `platform_wakeup_timer_arm(seconds)` computes a `COMP0` compare value
  from `seconds` and the known 32.768 kHz LFACLK, enables the RTC
  compare-0 interrupt, and starts the counter. `platform_wakeup_timer_clear()`
  clears the compare interrupt flag. Exact register/emlib call names
  need confirming against the vendored header (see Open Questions) —
  this project's established convention of flagging "verify against
  reference manual" applies here just as it did for the LPC810's WKT and
  the STM32's RTC wakeup timer.
- **Sleep mode**: EM2 (Deep Sleep) — retains RAM and CPU state, keeps the
  RTC/LFXO running, matches `vault_core`'s resume-in-place assumption
  (the same requirement that shaped the LPC810's Power-down-not-Deep-power-down
  choice and the STM32's Stop-2-not-Standby choice). Entered via
  `EMU_EnterEM2(true)` (retain latched pin states) — confirm this is the
  correct emlib call and that it actually blocks until an enabled
  wake source (the RTC compare interrupt) fires.

**EM3 was considered and rejected.** EM3 (Stop Mode) draws less current
than EM2 (0.6 µA vs. 0.9 µA per the board datasheet), and the watchdog
(WDOG) can keep running in EM3 via its own always-on, independent 1 kHz
ULFRCO (`WDOG_CTRL_EM3RUN`) — a theoretically viable EM3 wake source. But
this generation's WDOG (confirmed against Silicon Labs' emlib API docs)
has no interrupt capability at all: a timeout only ever triggers a full
chip reset, never a clean interrupt-based wake. Using it would mean
`platform_enter_low_power_sleep()` never actually returns — the chip
restarts from `Reset_Handler`/`main()` every cycle instead of resuming
mid-function — breaking `vault_core`'s resume-in-place assumption, and
concretely discarding the master-configured `WAKE_INTERVAL_SEC` every
cycle (`vault_core_init()` unconditionally resets it to the default on
every boot). Fixable in principle (EM3 does retain RAM, so the interval
could survive if `vault_core_init()` stopped unconditionally overwriting
it), but that's a real change to shared `core/` code, not a backend-local
detail, for ~0.3 µA of savings. Decision: stay with EM2/RTC-based
resume-in-place.

## 6. I2C0 slave driver

`platform_i2c_slave_init(addr)` configures `PD6`/`PD7` as I2C0 SDA/SCL
(open-drain, correct `ROUTE`/`LOCATION`), sets the slave address, and
enables the I2C0 interrupt in the NVIC. `I2C0_IRQHandler` decodes the
peripheral's interrupt flags (address match, RX data, TX data, STOP) and
calls the existing `vault_i2c_registers_on_write_byte()` /
`_on_read_request()` / `_on_stop()` hooks — the same pattern both other
backends already use, just against EFM32's I2C peripheral's own state
machine and flag names instead of LPC81x's `SLVSTATE` or STM32's HAL
listen-mode callbacks. emlib's `em_i2c.c` is written primarily for
master-mode polled transfers; EFM32 slave-mode operation may need more
direct flag/register handling than a thin emlib wrapper provides — expect
this driver to sit closer to raw register access than the STM32 I2C
backend does, similar in spirit to how the LPC810 I2C0 driver already
works entirely via direct `STAT`/`SLVCTL` register access.

Bus speed: match the existing 400 kHz Fast-mode target already
established for both other backends, once real hardware timing can be
verified.

### 6.1 Idle power while waiting for I2C

`core/vault_core.c`'s `WAKE_MAIN` wait loop now calls a new HAL contract
function, `platform_wait_for_interrupt()`, once per iteration instead of
busy-spinning — added across all three backends (this design's `emlib`
implementation should just call `__WFI()`, matching the LPC810/STM32U031
backends' plain Cortex-M `WFI` implementations). This halts the CPU core
clock until any enabled interrupt (I2C0's, in particular) fires, cutting
active-mode power during the — possibly lengthy — window the main MCU is
powered but not actively transacting on the bus at that exact instant.

**Future enhancement, not part of this implementation:** the board's own
feature list claims I2C "address recognition in Stop Mode" — i.e.
potentially deeper than plain `WFI`, an actual EM2-class sleep *during*
`WAKE_MAIN` that only wakes on this device's own I2C address match,
rather than gating just the CPU clock while every peripheral keeps
running. This would cut power further than `platform_wait_for_interrupt()`
alone, but needs its own follow-up work before it's safe to build:

- Confirm against the EFM32G210 reference manual (not just the board's
  summary datasheet bullet) exactly which energy mode "Stop Mode" refers
  to here, and whether address recognition needs a dedicated low-power
  compare path independent of the main I2C peripheral clock (the same
  kind of "separate always-on path" the RTC wake needed via LFXO).
- This would need its own new optional HAL hook (distinct from
  `platform_wait_for_interrupt()`, since going to EM2 mid-transaction has
  real implications HAL_PWREx-class deep sleep doesn't for a mid-loop
  WFI) — LPC810/STM32U031 would implement it as a no-op or fall back to
  `platform_wait_for_interrupt()`, EFM32 would implement real EM2 entry.
- Verify it correctly resumes the I2C peripheral's own state machine
  (address match, RX/TX flag handling in `I2C0_IRQHandler`) after waking
  from a deeper sleep than plain `WFI` leaves it in — plain `WFI` never
  stops the I2C peripheral clock at all, so this is a materially
  different resume path, not just "the same thing but deeper."

## 7. Debug UART

USART1 TX-only on `PC0`, 57600 8N1 — matching the fixed baud rate already
established for both other backends (`VAULT_LOG_ENABLED` gate, `vault_log()`
contract from `core/vault_log.h`). RX (`PC1`) intentionally left
unconfigured, matching the TX-only pattern used on both existing backends.

## 8. Build integration

- `VAULT_TARGET=efm32g210` added to the top-level `CMakeLists.txt`'s
  target dispatch, alongside `host`/`lpc810`/`stm32u031`.
- `VAULT_CONTEXT_SIZE` default: 320 bytes, matching both existing
  backends (same RadioLib-driven requirement; trivial against this part's
  16 KB RAM).
- `VAULT_LOG_ENABLED` opt-in pattern reused unchanged — conditionally
  compiles `platform_efm32g210_uart.c` and defines `VAULT_LOG_ENABLED`,
  exactly as the other two backends' `CMakeLists.txt` already do.
- `vault_core`'s existing `-mcpu`/`-mthumb`/`-Os` compile-option keying
  (in `core/CMakeLists.txt`, keyed off `VAULT_TARGET`) needs a third
  branch for `efm32g210`, using `-mcpu=cortex-m3` (not `cortex-m0plus` —
  this is a genuinely different core than the other two backends).

## 9. Open questions / verify during implementation

Consistent with this project's established practice of flagging
unverified register-level details rather than presenting invented
confidence:

- Exact Gecko SDK / emlib source tree layout and file names once vendored
  (Silicon Labs has reorganized their SDK distribution over the years;
  confirm against whatever's actually downloaded, don't assume a specific
  historical layout).
- `I2C0->ROUTE` / `USART1->ROUTE` `LOCATION` field values for the
  `PD6`/`PD7` and `PC0`/`PC1` pin pairs specifically — the board schematic
  strongly implies these pins are valid alternate locations for these
  peripherals, but the exact `LOCATION` index must come from the
  reference manual's alternate-function table, not be guessed.
- RTC `COMP0`-based wake mechanism: exact register/emlib function names,
  and whether any additional NVIC/wake-source-enable step (like the
  LPC810's `STARTERP1` or the STM32's EXTI-line requirement) exists for
  RTC-triggered EM2 wake on this specific part.
- `EMU_EnterEM2()`'s exact retention/wake behavior — confirm it genuinely
  blocks until wake and preserves full CPU/RAM state, matching
  `vault_core`'s resume-in-place assumption, before relying on it.
- Whether `SysTick_Handler` needs the same explicit `HAL_IncTick()`-style
  real implementation this project already hit as a hard bug on the
  STM32U031 backend — if this backend uses any blocking/timeout logic
  that depends on a system tick (emlib itself may not need one the way
  ST's HAL does), confirm rather than assume it's fine.
- Real load capacitor values for both on-board crystals — Olimex's own
  schematic lists the populated capacitor values; confirm they're
  correctly matched to Q1/Q2's rated load capacitance before trusting
  LFXO/HFXO startup blindly, same lesson learned from the STM32 crystal
  investigation.

## 10. Out of scope

- Any change to `core/`, the I2C register-map protocol, or wake-scheduling
  semantics — this is purely a new HAL backend.
- Power-switching circuit design for a main sensor MCU — already covered
  generically in `docs/I2C_INTEGRATION_GUIDE.md` and applies unchanged.
- LPC810 Task 11 remaining verification items — unrelated, separate
  work.
