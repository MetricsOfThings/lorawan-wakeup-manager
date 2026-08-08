# LoRaWAN Wakeup Manager — Firmware Design

Status: approved for implementation planning
Date: 2026-08-08
Related: [docs/MCU_Analysis_Report.md](../../MCU_Analysis_Report.md)

## 1. Purpose and scope

Build the firmware for the "Data Vault" auxiliary MCU described in the MCU
Analysis Report: a device that stays powered continuously at sub-microamp
current, keeps an RTC-driven wake schedule, retains an opaque LoRaWAN session
context blob across the main Application Processor's power-off intervals,
and hands that context to the main MCU over I2C on each wake cycle.

This POC targets the **Data Vault side only**. The main Application
Processor is represented by a simple I2C-master test harness that exercises
the register protocol below — no real LoRaWAN stack is required for this
phase.

Two MCU targets are in scope over time:
- **LPC810** (available now) — first hardware bring-up target.
- **STM32U031F8P6** (arriving later) — the production part selected in the
  analysis report. Its backend is stubbed in this phase, implemented once
  hardware is available.

The firmware core must be identical source code across both targets. Only
the platform backend differs.

## 2. Architecture

Three layers, backend chosen at CMake configure time (`-DVAULT_TARGET=...`),
not at runtime — a shipped board is always exactly one MCU, so a
function-pointer vtable would add indirection with no benefit.

```
core/            Platform-agnostic. No vendor SDK, no MCU headers.
                 Unit-testable on the host.
platform.h       The HAL contract: function prototypes every backend
                 must implement.
platform/lpc810/     Backend: bare CMSIS + NXP LPC81x register headers.
platform/stm32u031/  Backend: stubbed now; implemented later against
                     ST's HAL/LL, behind the same contract.
platform/host_mock/  Backend: fakes for unit tests, no hardware.
```

`core/` never includes a vendor header. Porting to the STM32U031F8P6 means
writing `platform/stm32u031/*.c` against `platform.h` — `core/` does not
change.

## 3. HAL contract (`platform.h`)

```c
void     platform_init(void);
void     platform_wakeup_timer_arm(uint32_t seconds);
void     platform_wakeup_timer_clear(void);
void     platform_main_rail_enable(bool on);
void     platform_i2c_slave_init(uint8_t addr);
void     platform_i2c_slave_deinit(void);      // MspDeInit-equivalent
void     platform_bus_isolate(void);           // I2C pins -> analog, no pull
void     platform_enter_low_power_sleep(void); // blocks; returns after wake
```

I2C is event-driven, not a blocking "exchange" call, since register-map
semantics belong in `core/` and must be shared verbatim between backends.
The platform backend's I2C slave ISR drives these `core/`-provided hooks:

```c
void    vault_i2c_on_write_byte(uint8_t byte);
uint8_t vault_i2c_on_read_request(void);
void    vault_i2c_on_stop(void);
```

`core/vault_i2c_registers.c` owns the register pointer, auto-increment
behavior, bounds clamping, and command dispatch described in section 5.
Backends only translate their specific I2C peripheral's interrupt events
into these three calls.

## 4. Context storage

The vault treats the LoRaWAN session context as an **opaque byte blob** —
it never parses DevAddr, keys, or frame counters. That is the main MCU's
concern; decoupling it here means the vault doesn't need to change when the
main MCU's LoRaWAN stack or protocol version changes.

- Buffer: `uint8_t vault_context[VAULT_CONTEXT_SIZE]`, `VAULT_CONTEXT_SIZE`
  a compile-time constant per target.
- **LPC810 default: 64 bytes.** The LPC810 has only 1 KB of total SRAM,
  which must also hold the stack and all other statics — the analysis
  report's "production" context estimate (256–512 bytes) would consume a
  quarter to half of the entire chip's RAM by itself. 64 bytes is enough
  to exercise the protocol end-to-end without starving the rest of the
  firmware; it is not meant to hold a full production LoRaWAN 1.1 context.
- **STM32U031F8P6:** larger default set when that backend is implemented;
  its 12 KB SRAM has no comparable constraint.
- `CONTEXT_LENGTH` (register 0x02, see below) tracks how many of those
  bytes are actually meaningful, since the real LoRaWAN context size
  differs between protocol versions (see the MCU Analysis Report's
  correction: 40 bytes for 1.0.x, 80 for 1.1) and is smaller than the
  buffer's compile-time maximum.
- **Retention verification (LPC810, do once hardware is available):**
  confirm that waking from Power-down mode resumes execution in place
  rather than re-running the reset/startup sequence. If it does reset,
  `vault_context` and the other retained state variables need an explicit
  linker section rather than ordinary statics. The design assumes
  resume-in-place, consistent with the STM32 Stop 2 behavior described in
  the analysis report; this assumption is unverified for LPC810 Power-down
  mode until tested on hardware.

## 5. I2C protocol

Main MCU is I2C master, vault is I2C slave. Access pattern is the standard
register-map convention used by I2C EEPROMs and sensors: the master writes
a register address byte to set a pointer, then either writes bytes
(auto-incrementing from that pointer) or performs a repeated-start read to
pull bytes back from it.

All multi-byte fields are little-endian (both MCUs are little-endian
Cortex-M — no conversion needed).

| Reg | Name | Access | Size | Meaning |
|---|---|---|---|---|
| `0x00` | `STATUS` | R | 1 B | bit0 `CONTEXT_VALID` (0 until a context has been stored at least once), bits 1-7 reserved, always read as 0 in this POC (bit1 is earmarked for a future non-timer wake reason once event-triggered wake exists — see section 8) |
| `0x01` | `PROTOCOL_VERSION` | R | 1 B | Register-map version. Lets the main MCU confirm compatibility before trusting the layout above. Starts at `0x01`. |
| `0x02` | `CONTEXT_LENGTH` | R/W | 1 B | Number of meaningful bytes in `CONTEXT_DATA` (≤ `VAULT_CONTEXT_SIZE`). Main MCU sets this when writing back an updated context. |
| `0x03` | `CONTEXT_DATA` | R/W | up to `VAULT_CONTEXT_SIZE` | The opaque retained blob. |
| `0x04` | `COMMAND` | W | 1 B | `0x01` = `CMD_DONE`: main MCU has finished; vault isolates the bus and sleeps after the I2C STOP condition. Other values reserved/ignored. |
| `0x05` | `WAKE_INTERVAL_SEC` | R/W | 4 B (u32 LE) | Seconds until the next wake. Vault arms the RTC/wakeup timer with this value on sleep entry. Readable for confirmation/debugging. |

Rules:
- Reads/writes past `CONTEXT_LENGTH` or `VAULT_CONTEXT_SIZE` are clamped,
  never treated as an error — a malformed or buggy master must not be able
  to hang or corrupt the vault.
- `CMD_DONE` only takes effect **after** the STOP condition, not
  mid-transaction, so it cannot race a write that follows it in the same
  transaction.
- `WAKE_INTERVAL_SEC` is scheduling ownership split: the main MCU decides
  *how often* to report (business logic — "every 5 minutes"), the vault
  autonomously *enforces* the schedule with its RTC/timer, since it is the
  only thing awake between cycles. The vault does not have an opinion on
  what the right interval is.

## 6. Wake scheduling and state machine

```
COLD BOOT (first-ever power-on, no prior state)
  context_valid = false
  wake_interval_sec = DEFAULT_WAKE_INTERVAL_SEC   (compiled-in constant)
  → go directly to WAKE_MAIN, no initial wait

WAKE_MAIN
  platform_main_rail_enable(true)
  platform_i2c_slave_init(VAULT_I2C_ADDR)
  block, servicing register access via the I2C hooks in section 3,
  until CMD_DONE is received

BUS_ISOLATION
  platform_i2c_slave_deinit()
  platform_bus_isolate()
  platform_main_rail_enable(false)

ARM_SLEEP
  platform_wakeup_timer_arm(wake_interval_sec)
  platform_enter_low_power_sleep()   // blocks; resumes here on wake

  → back to WAKE_MAIN
```

`wake_interval_sec` and `vault_context`/`context_valid` are plain SRAM
state owned by `core/`, updated directly by the register write handlers —
no separate synchronization step is needed between "master wrote a
register" and "the state machine sees the new value."

**Persistence:** `wake_interval_sec` lives in SRAM only. It survives every
normal sleep cycle (the vault itself never powers off in normal operation)
but resets to `DEFAULT_WAKE_INTERVAL_SEC` if the vault's own supply is ever
fully lost (e.g., battery replacement) — until the main MCU writes a new
value on its next wake. This is a deliberate simplification: persisting the
interval across a full vault power loss would require flash/EEPROM storage
and wear management, which is out of scope for this POC.

**First boot:** the very first wake happens immediately, without waiting
out a full default interval, so the main MCU can perform its initial OTAA
join and start reporting without an arbitrary extra delay.

## 7. Build and test

- **Build system:** CMake, target selected via `-DVAULT_TARGET=host|lpc810|stm32u031`.
- **`host` target:** compiles `core/` against `platform/host_mock/` with a
  native compiler. No hardware required. This is what makes the
  "identical core across both MCUs" claim actually verifiable, and where
  the register-map logic in `vault_i2c_registers.c` gets exercised without
  needing an I2C bus at all.
- **`lpc810` target:** `arm-none-eabi-gcc`, Cortex-M0+, LPC810-specific
  linker script (4 KB flash / 1 KB SRAM).
- **`stm32u031` target:** placeholder only in this phase — CMake target
  exists and fails clearly if built, real backend added when hardware
  arrives.
- **Test runner:** a minimal custom assert-based harness, not an external
  framework like Unity/CMock — no dependency management needed at this
  project size.

## 8. Explicitly out of scope for this POC

- Real LoRaWAN stack integration (main MCU side is a test harness).
- STM32U031F8P6 backend implementation (stubbed only).
- Wake-interval persistence across a full vault power loss.
- Event-triggered/out-of-band wake (only interval-based scheduling).
- External crystal on LPC810 (using internal RC oscillator; RTC timing
  accuracy is a known limitation on this bring-up target, not present on
  the STM32U031F8P6).
