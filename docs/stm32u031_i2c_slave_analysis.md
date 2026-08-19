# STM32U031 I2C Slave Communication Analysis

> **Post-mortem note (added after the actual root cause was found — see
> git history and `platform_stm32u031.c`'s `platform_i2c_slave_init()`
> comment for the full story):**
>
> This document was written mid-investigation and is a mix of verified
> findings and claims that turned out not to hold up. Read it with that
> in mind, not as settled fact.
>
> - **Issues #6 (missing GPIO speed) and #7 (analog filter never
>   configured) were real, verified gaps** — confirmed against the live
>   code, fixed, and kept.
> - **Issues #1–#4 (all about the read path) were checked directly
>   against the vendored HAL source and the real host code, and do not
>   apply.** Issue #1 assumes the host uses STOP between a register-
>   pointer write and a data read; the actual host code always uses
>   repeated START (`endTransmission(false)`), which the doc itself
>   says is the case that works fine. Issues #2/#3 claim every NACK-
>   terminated read routes through `HAL_I2C_ErrorCallback()`, skipping
>   `SlaveTxCpltCallback()` — traced through `I2C_Slave_ISR_IT()` and
>   `I2C_ITSlaveSeqCplt()` in `stm32u0xx_hal_i2c.c` and confirmed false
>   for this codebase's always-arm-one-byte pattern (`XferCount` is
>   always 0 by the time NACK arrives, so the normal completion branch
>   runs, which does call `SlaveTxCpltCallback()`). This also matches
>   real hardware evidence: reads worked throughout the whole
>   debugging session.
> - **The actual root cause was two compounding factors this document
>   never considered:** a documented Nordic nRF52840 host silicon
>   errata (TWI anomaly #149 — the first clock pulse after the slave
>   exits clock stretching can be too short or lost), confirmed by
>   swapping to a different host MCU; and a genuine timing-margin
>   sensitivity independent of that errata (the replacement host also
>   failed at ~108 kHz and only worked at ~50 kHz). Issue #7 below
>   ("Diagnostic ~50 kHz Timing with Active Analog Filter") ended up
>   closer to the real story than the read-path issues, though it
>   didn't identify the actual mechanism either.

## Project Context

The wakeup manager firmware runs on a slave MCU (STM32U031) communicating with a main MCU via I2C. The I2C stack uses the ST HAL with a sequential one-byte-at-a-time transfer model, with all three backends implementing the same register protocol.

**Key reference files:**
- `platform/stm32u031/src/platform_stm32u031.c` — STM32U031 I2C implementation (384 lines)
- `core/src/vault_i2c_registers.c` — Platform-agnostic register protocol (230 lines)
- `core/src/vault_core.c` — Main orchestrator (90 lines)
- `platform/lpc810/src/platform_lpc810_i2c.c` — LPC810 reference (139 lines)
- `platform/efm32g210/src/platform_efm32g210_i2c.c` — EFM32G210 reference (287 lines)

---

## Issue #1: `AddrCallback` Read Path Assumes Pointer-Write Pointer Survived STOP

**Severity: Critical**
**Files:** `platform_stm32u031.c:287-294`, `vault_i2c_registers.c:181-193`

The standard I2C read pattern requires the master to first write the register address, then read the data:

```
START + addr+W + reg_addr + [repeated START] + addr+R + data... + STOP
```

When the master writes a pointer byte (e.g., `0x00` for STATUS), `on_write_byte()` sets `s_have_pointer = true` and `s_active_reg = REG_STATUS`.

**If the host sends STOP after the pointer byte** (separate transactions):
- `on_stop()` fires → `s_have_pointer = false`, `s_active_reg = REG_NONE`
- When the subsequent `addr+R` address match fires, `AddrCallback` calls `on_read_request()` with `s_have_pointer == false`
- `on_read_request()` at line 142-143 returns `0xFF` for every byte because the pointer state was wiped

**If the host uses repeated START** (no STOP between pointer-write and data-read):
- `s_have_pointer` survives because `on_stop()` was never called
- The read works correctly

The LPC810 backend explicitly handles `SLVDESEL` (line 113), which fires on both STOP and repeated START to a different address. The STM32U031 HAL's `AddrCallback` fires on every address match, including repeated START to the same slave, but there's no separate callback for "deselected on repeated START."

**Symptom:** If the host's I2C library (e.g., Arduino Wire) issues separate transactions with STOP between the pointer-write and data-read, **all register reads return 0xFF**.

**Verification:** Check the host-side code to confirm whether it uses combined `beginTransmission() / endTransmission() / requestFrom()` (which may or may not use repeated START depending on the Wire library implementation), or whether it uses `transfer()` style calls with explicit repeated START.

---

## Issue #2: NACK on Slave TX Routes to Error Path, `SlaveTxCpltCallback` Never Fires

**Severity: High**
**File:** `platform_stm32u031.c:316-318`

When the master NACKs a byte during a read (the normal way the master terminates a read):

1. HAL sets `ErrorCode = HAL_I2C_ERROR_AF` (acknowledge failure)
2. Since `XferOptions == I2C_NEXT_FRAME`, the HAL calls `I2C_ITError()`, which resets HAL state
3. On the subsequent STOP: `I2C_ITSlaveCplt()` sees `ErrorCode != NONE` → calls `ListenCpltCallback` only
4. **`SlaveTxCpltCallback` is never invoked** for the terminating byte

The `SlaveTxCpltCallback` is where `on_read_request()` gets called to fetch the next byte to transmit. Since it never fires on the final byte, `s_field_offset` never increments for that last byte.

When `ListenCpltCallback` fires, `on_stop()` resets `s_field_offset = 0` and `s_active_reg = REG_NONE`. For single-byte reads (STATUS, PROTOCOL_VERSION) this works because only one byte is needed. For multi-byte reads (CONTEXT_LENGTH = 2 bytes, WAKE_INTERVAL_SEC = 4 bytes), the state tracking is silently inconsistent with what the master actually received.

Additionally, every read transaction ends through an error code path, which sets `s_last_i2c_error` in `HAL_I2C_ErrorCallback` but takes no recovery action. Any residual HAL error state from one transaction persists into the next.

---

## Issue #3: `SlaveTxCpltCallback` Always Arms `I2C_NEXT_FRAME`, Never `I2C_LAST_FRAME`

**Severity: High**
**File:** `platform_stm32u031.c:316-318`

```c
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    s_tx_byte = vault_i2c_registers_on_read_request();
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
}
```

The slave has no way to know the master will NACK the next byte. Always arming with `NEXT_FRAME` tells the HAL "more data follows." When the master NACKs + STOPs, the HAL's error path fires instead of clean completion, as described in Issue #2.

The RX path benefited from the `next_write_byte_is_last()` fix to correctly signal `I2C_LAST_FRAME` for the final byte of fixed-length registers. No equivalent optimization exists for the TX path, though it's harder to implement since the master controls read length.

**Impact:** Every read transaction ends in `HAL_I2C_ERROR_AF`. This could leave residual HAL state (error flags, stale pointer values) that corrupts subsequent transactions.

---

## Issue #4: `on_read_request()` Called Multiple Times per Address Match, Always Increments `s_field_offset`

**Severity: High**
**File:** `vault_i2c_registers.c:139-179`, `platform_stm32u031.c:292-293`

In the `AddrCallback` read path:

```c
s_tx_byte = vault_i2c_registers_on_read_request();  // called from AddrCallback
HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
```

`on_read_request()` increments `s_field_offset` on every call (line 177). This means the **first call** from `AddrCallback` advances the offset before the byte even leaves the peripheral. When `SlaveTxCpltCallback` fires for the next byte, `s_field_offset` is already 2 instead of 1.

For STATUS (1-byte register): first call returns correct value at offset 0, offset goes to 1. Second call (if master reads more) returns 0x00 at offset 1 since the CONDITION `s_field_offset == 0` fails. This is acceptable since the host should only read one byte.

For CONTEXT_LENGTH (2-byte register): `AddrCallback` calls `on_read_request()` → offset 0 → returns low byte → offset becomes 1. `SlaveTxCpltCallback` calls `on_read_request()` → offset 1 → returns high byte → offset becomes 2. This works but relies on the `AddrCallback` being responsible for the first byte's offset increment, which differs from the write path where `on_write_byte()` handles it.

If `s_have_pointer` is false (see Issue #1), every call returns 0xFF, and the 0xFFs are transmitted to the master. This is the dominant failure mode for reads.

---

## Issue #5: `OwnAddress1` Address Encoding Requires Careful Verification

**Severity: Medium**
**File:** `platform_stm32u031.c:249`

```c
s_i2c_handle.Init.OwnAddress1 = (uint32_t)(addr << 1);
```

The 7-bit address `0x42` is shifted left by 1 to produce `0x84`. The HAL's `HAL_I2C_Init()` writes this directly to OAR1:

```c
hi2c->Instance->OAR1 = (I2C_OAR1_OA1EN | hi2c->Init.OwnAddress1);
```

The OAR1 register format for 7-bit addressing:
- Bit [15]: OA1EN (address match enabled)
- Bits [7:1]: OA1[7:1] (the 7-bit address)
- Bit [0]: Reserved/0

With `OwnAddress1 = 0x84` (0b10000100), the address occupies bits [7:1] as `0b1000010`, which is `0x42`. This is correct.

However, the HAL documentation for master-side device addresses explicitly states "The device 7 bits address value in datasheet must be shifted to the left before calling the interface" (`stm32u0xx_hal_i2c.c:5736-5737`). The slave-side `OwnAddress1` has no such documented convention in the HAL header. The current code works, but the double-shifting convention between master and slave address configuration is easy to confuse. If the HAL behavior changes between Cube versions, this could silently break.

**Recommendation:** Verify against the specific CubeMX/HAL version in use that `OwnAddress1` for 7-bit slave mode expects the pre-shifted value (not the raw 7-bit value). The `IS_I2C_OWN_ADDRESS1` macro in the HAL header constrains the valid range — values above `0xFE` may fail the assertion check.

---

## Issue #6: Missing GPIO Speed Configuration

**Severity: Medium**
**File:** `platform_stm32u031.c:193-206`

```c
GPIO_InitTypeDef gpio_init = {0};
gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
gpio_init.Mode = GPIO_MODE_AF_OD;
gpio_init.Pull = GPIO_NOPULL;
gpio_init.Alternate = GPIO_AF3_I2C2;
// gpio_init.Speed is NOT set — defaults to GPIO_SPEED_FREQ_LOW
```

The `Speed` field defaults to 0 (`GPIO_SPEED_FREQ_LOW` — ~2 MHz max). For I2C operation in slave mode, the GPIO input Schmitt trigger speed affects edge detection reliability. At 100 kHz this is likely fine, but at 400 kHz Fast-mode, slow GPIO could miss fast edges or introduce setup/hold time violations on the receive path.

None of the other backends explicitly set GPIO speed either (they use peripheral-level clock divisors instead).

---

## Issue #7: Diagnostic ~50 kHz Timing with Active Analog Filter

**Severity: Medium**
**File:** `platform_stm32u031.c:248`

```c
s_i2c_handle.Init.Timing = 0x0020242A;  // ~50 kHz diagnostic value
```

The HAL comment at line 227-247 documents this as a diagnostic value. Breaking down the timing:
- PRESC=0, SCLDEL=2, SDADEL=0, SCLH=36, SCLL=42
- t_I2CCLK=250ns, SCLH=9250ns, SCLL=10750ns, t_SCL=20000ns → ~50 kHz

In slave mode, the Timing register primarily controls:
1. **Digital noise filter (DNF):** PRESC=0 → filter clock = PCLK1 = 4 MHz → filters pulses < 250ns
2. **SCL setup/hold times:** SCLDEL=2 → 500ns setup before SDA sampling
3. **SDA output delay:** SDADEL=0

If the host drives at 100 kHz or 400 kHz, the DNF threshold (250ns) is well below any SCL period edge-to-edge, so DNF itself isn't the problem. However, the **analog digital filter** (ADNF) is never configured. `HAL_I2CEx_ConfigAnalogFilter()` is never called, and `HAL_I2CEx_DisableAnalogFilter()` is never called either. The analog filter defaults ON with ~150ns filter time, which can attenuate fast edges at 400 kHz and push SDA sampling closer to setup/hold margins.

The HAL's internal timing computation for CR2 AUTOEND is set at init (line 625 of the HAL):
```c
hi2c->Instance->CR2 |= (I2C_CR2_AUTOEND | I2C_CR2_NACK);
```

AUTOEND is set by default. While documented as master-mode only, having it on in slave mode is harmless and is properly cleared/managed by the HAL during active transfers.

---

## Issue #8: `platform_enter_low_power_sleep()` Clock Reconfiguration

**Severity: Low**
**File:** `platform_stm32u031.c:343-358`

```c
void platform_enter_low_power_sleep(void) {
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
    HAL_ResumeTick();
    SystemClock_Config();
}
```

`SystemClock_Config()` reconfigures the system clock after Stop 2 wakeup. If `SystemClock_Config()` fails or hangs during the reconfiguration phase (e.g., oscillator stabilization timeout), the CPU will be stuck post-wakeup with I2C not yet re-initialized, causing a silent hang. No timeout or error recovery path exists.

This is a general firmware robustness concern rather than an I2C-specific issue.

---

## Issue #9: `s_i2c_handle` Stale State After Stop 2 Wakeup Cycle

**Severity: Low**
**File:** `platform_stm32u031.c:32`

```c
static I2C_HandleTypeDef s_i2c_handle;
```

After Stop 2 wakeup, SRAM contents are retained. When `vault_core_step()` calls `platform_i2c_slave_init()` for the next wake cycle, `s_i2c_handle` retains stale fields from the previous `DeInit`. `HAL_I2C_Init()` overwrites key fields (`State`, `ErrorCode`, `Mode`) but doesn't zero the entire struct. Fields like `XferCount`, `XferSize`, `pBuffPtr`, `XferOptions`, `pXferPend` retain stale values until `HAL_I2C_EnableListen_IT()` or the first address match overwrites them.

In practice, `HAL_I2C_EnableListen_IT()` clears most of these, but any field the HAL reads before overwriting could trigger unexpected behavior. The `HAL_I2C_DeInit()` at line 266 clears the peripheral registers but doesn't zero the handle struct.

---

## Issue #10: NVIC Priority Sharing Between I2C and RTC

**Severity: Low**
**File:** `platform_stm32u031.c:257`, `platform_stm32u031.c:134`

```c
HAL_NVIC_SetPriority(I2C2_3_IRQn, 0, 0);
HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 0, 0);
```

Both I2C and RTC share the highest NVIC priority level. During the I2C wait loop (which shouldn't have RTC firing), this isn't an issue. But if the RTC wakeup timer somehow fires during active I2C communication (misconfigured timer, LSI drift), the RTC ISR would preempt the I2C ISR, potentially delaying the I2C response beyond what the master expects.

I2C2_3_IRQn (IRQ 24) is shared between I2C2 and I2C3. I2C3 is never initialized, so spurious I2C3 interrupts shouldn't occur. However, if I2C3 were to receive a spurious interrupt, the handler only passes `s_i2c_handle` (the I2C2 handle) to `HAL_I2C_EV_IRQHandler()`, which could corrupt the I2C2 state machine if I2C3's peripheral flags are non-zero.

---

## Cross-Backend Comparison

| Concern | LPC810 | EFM32G210 | STM32U031 |
|---|---|---|---|
| STOP on repeated START | `SLVDESEL` fires on STOP and repeated START to other address — `on_stop()` called, but repeated START to same address does NOT fire SLVDESEL | `SSTOP` fires explicitly on STOP only — repeat START to same address does NOT fire SSTOP | HAL `AddrCallback` fires on every address match; `ListenCpltCallback` fires on STOP (and may fire on repeated START, HAL-dependent) |
| Read termination | NACK from master detected via peripheral state machine; no error callback concept | SSTOP interrupt + manual TXBL disable on STOP | NACK routes to `HAL_I2C_ERROR_AF` → error callback → `SlaveTxCpltCallback` never fires |
| Clock stretching | Explicit: SLVCONTINUE must be written to release SCL | Explicit: manual `I2C_CMD_ACK` releases clock stretch after each byte consumed | HAL-managed: `I2C_NOSTRETCH_DISABLE` enabled, HAL handles internally |
| GPIO speed | Peripheral clock divisor handles timing | N/A (different GPIO model) | **Not configured** — defaults to low speed |
| Error recovery | No error callback — all errors are state-machine transitions | No error callback — all errors are flag-based | `HAL_I2C_ErrorCallback` captures state but performs no recovery |

---

## Most Likely Root Causes of Observed I2C Failures

Based on the hardware testing notes in the code comments (COMMAND register value byte getting NACK'd at both ~108 kHz and ~50 kHz), the likely sequence of observed symptoms:

1. **Read transactions returning 0xFF** — caused by Issue #1, if the host sends STOP (not repeated START) between the pointer-write and data-read phases. This would affect ALL register reads, not just specific registers.

2. **Long context writes working but short writes failing** — consistent with the `I2C_LAST_FRAME` fix being correct. Before the fix, the COMMAND register (2-byte total: pointer + 1 value) was the first to expose the re-arm timing issue. Longer writes (320+ bytes for context data) always used `NEXT_FRAME` for intermediate bytes and the last byte happened to align with STOP correctly, making the last-byte race less visible.

3. **Intermittent communication failures** — caused by Issue #3/#4: read transactions ending through the error path leaves residual HAL state that can corrupt the next transaction, especially when `s_last_i2c_error` isn't cleared and stale `XferOptions` or `ErrorCode` values persist in the HAL handle.

4. **Silent data corruption on reads** — if all reads return 0xFF consistently, the host may interpret this as "device not present" or "I2C bus error" and retry or timeout, masking the actual root cause.

## Recommended Investigation Steps

1. **Verify host I2C transaction pattern:** Add bus-level logging or use a logic analyzer to confirm whether the host uses repeated START or STOP+new-START between pointer-write and data-read. This determines whether Issue #1 is the dominant failure mode.

2. **Add HAL state logging in callbacks:** In `HAL_I2C_AddrCallback`, log `s_have_pointer`, `s_active_reg`, and `s_field_offset` at entry. If `s_have_pointer == false` on any read-direction address match, Issue #1 is confirmed.

3. **Check `s_last_i2c_error` after transactions:** After each I2C transaction completes, check the value of `s_last_i2c_error` and `s_last_i2c_isr`. Persistent `HAL_I2C_ERROR_AF` values would confirm the TX error path issue.

4. **Disable analog filter:** Call `HAL_I2CEx_DisableAnalogFilter(&s_i2c_handle)` after `HAL_I2C_Init()` to eliminate the analog filter as a variable.

5. **Set GPIO speed:** Add `gpio_init.Speed = GPIO_SPEED_FREQ_HIGH` (or at least `GPIO_SPEED_FREQ_MEDIUM`) in `i2c_pins_init()`.

6. **Clear HAL error state in `ListenCpltCallback`:** Before re-entering listen mode, explicitly clear `s_i2c_handle.ErrorCode = HAL_I2C_ERROR_NONE` to prevent residual error state from corrupting the next transaction.
