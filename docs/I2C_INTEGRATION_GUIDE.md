# Wakeup Manager I2C Integration Guide

**Audience:** firmware developers implementing the I2C **master** side — i.e.
the sensor / LoRaWAN Application Processor MCU that wants to use the Data
Vault for wake scheduling and LoRaWAN session retention.

**You do not need to read the Vault's own source code to use it.** This
document, plus the register map in section 3, is the complete contract.

---

## 1. What the Vault does for you

The Vault is a separate, always-on companion MCU next to your sensor MCU.
Between transmissions, your MCU is **completely unpowered** — the Vault cuts
its power rail entirely. The Vault:

1. Keeps a wake schedule (an interval timer) running while your MCU is off.
2. Turns your MCU's power rail on when it's time to wake up.
3. Holds your LoRaWAN session (DevAddr, session keys, frame counters — as an
   opaque blob it never inspects) in its own retained memory while your MCU
   is powered off, so you don't have to redo an OTAA join on every cycle.
4. Turns your MCU's power rail back off once you signal you're done, and
   goes back to sleep until the next scheduled wake.

Your MCU is the **I2C master**; the Vault is the **I2C slave**. There is no
separate "wake" signal line to wire up — **your MCU's own power-on /
reset is the wake signal.** When the Vault asserts your rail, your MCU boots
from reset exactly as if a battery had just been inserted.

---

## 2. Wiring

| Signal | Vault side (reference implementation pin) | Notes |
|---|---|---|
| I2C SDA | LPC810: `PIO0_1` · STM32U031F8P6: `PA10` | See §2.1 on pull-ups. On LPC810 this pin doubles as the chip's own ISP-entry pin — see the note below the table |
| I2C SCL | LPC810: `PIO0_4` · STM32U031F8P6: `PA9` | See §2.1 on pull-ups |
| Your MCU's power rail, switched | Driven by LPC810 `PIO0_0` · STM32U031F8P6 `PA0` | High = your rail powered on. See §2.2 for the switching circuit itself — this pin is a GPIO, not a power output |

**Why LPC810 uses `PIO0_1`/`PIO0_4` and not a nicer pair:** the LPC810 ships only in an 8-pin package, which brings out just `PIO0_0` through `PIO0_5` — six pins total, of which `PIO0_0` (rail enable), `PIO0_2`/`PIO0_3` (SWD), and `PIO0_5` (reset) are already committed, leaving exactly two free. `PIO0_1` is also the LPC810's own ISP-entry pin, sampled by its boot ROM on every reset; since this design intentionally puts I2C pull-ups on your MCU's switched rail rather than the Vault's own rail (§2.1), that line is unpowered during the Vault's own power-up, before your rail has ever been turned on. This is a known, accepted trade-off of the 8-pin package, not an oversight — worth being aware of if you're debugging a Vault that unexpectedly boots into ISP mode instead of running.

The Vault's I2C slave address is **`0x42`** (7-bit).

The pin numbers above are the reference firmware's defaults and may be
re-mapped per board — confirm against your actual schematic, since these are
firmware-configurable, not fixed by the I2C protocol itself.

### 2.1 Pull-up placement — read this before wiring

This is the one wiring detail that will silently break your battery life if
you get it wrong. **I2C pull-up resistors must be tied to your MCU's own
switched power rail — never to the Vault's always-on battery rail.**

- If the pull-ups sit on the Vault's always-on rail, then whenever your MCU
  is powered off, the Vault's own idle-low bus activity (or just static bus
  state) bleeds current through those resistors to your MCU's ESD diodes,
  even though your MCU is "off." This defeats the entire point of the
  design — the Vault's sub-microamp sleep current gets swamped.
- With pull-ups on your MCU's rail, when the Vault cuts your power, the
  pull-up supply itself drops to 0 V and the bus rests at ground with zero
  parasitic leakage.

The Vault firmware additionally puts its own SDA/SCL pins into a
high-impedance analog state (no internal pull-up/down) before it ever cuts
your rail, specifically so its GPIOs can't back-feed your powered-down MCU
through their ESD protection diodes. You don't need to do anything for this
on your side — just get the external pull-up placement right.

### 2.2 Power switching circuit — what actually sits between the Vault and your MCU's `VDD`

The Vault's rail-enable pin (`PIO0_0` on LPC810, `PA0` on STM32U031F8P6) is a
GPIO, not a power output — it can't itself deliver the current your MCU
needs. You need an external switch between the battery/regulated rail and
your MCU's `VDD`, controlled by that pin. Two things matter here: **switch
the supply side, not the ground side** (a P-channel high-side switch, not
an N-channel low-side one), and pick a device whose own off-state leakage
doesn't undo the Vault's sub-microamp sleep current.

**Why high-side, not low-side:** switching your MCU's ground return instead
of its supply reintroduces the exact parasitic-back-powering risk described
in §2.1 for the I2C lines — if your MCU's `VDD` stays connected while only
its ground floats, current can sneak back through its own I/O ESD diodes via
any other wire still connected to it. Switching the supply avoids this
entirely: when off, your MCU has no power source at all, full stop.

**Why a MOSFET (or dedicated load-switch IC) rather than a regulator's
`EN` pin, if your MCU shares the Vault's own regulated rail:** a bare
MOSFET's off-state leakage is pure semiconductor junction leakage, often
achievable in the single-digit-nanoamp range with a well-chosen part — the
same order of magnitude as the sleep currents this whole design targets. If
your MCU instead needs its *own* regulated voltage (different from the
Vault's), you need a regulator there regardless for the conversion, and
should use *that* regulator's own `EN`/shutdown pin instead of adding a
separate switch on top of it — just pick one with a genuinely low shutdown
current, since regulator shutdown modes commonly draw several microamps
continuously, which can end up dominating your total sleep current more
than the microcontroller itself does.

A classic 2-transistor high-side P-channel switch, using only common,
easily-sourced parts:

```
Battery / regulated rail ──┬──────────────┐
                            │              │
                          [R1, ~47k]     Source
                            │              │ (P-channel MOSFET,
Vault rail-enable pin ──[R2]● NPN base   Gate ──┘  e.g. BS250/AO3401)
                            │              │
                           NPN            Drain ── to your MCU's VDD
                            │              (e.g. 2N3904/BC547)
                           GND
```

- Vault drives the rail-enable pin **high** → the NPN turns on → pulls the
  P-channel MOSFET's gate down near ground → with its source sitting at the
  battery rail, that's a large negative `Vgs` → the MOSFET turns **on**,
  powering your MCU.
- Vault drives it **low** → NPN off → `R1` pulls the MOSFET's gate back up
  to the battery rail → `Vgs ≈ 0` → MOSFET turns **off**.
- `R1` (gate pull-up, ~47 kΩ) gives the gate a defined "off" state whenever
  the NPN isn't actively pulling it down — without it, the switch's state
  is undefined when idle.
- `R2` (NPN base resistor, ~10 kΩ) just limits base current from the
  Vault's GPIO.

This topology works with any small-signal P-channel MOSFET and NPN — it
doesn't depend on the MOSFET having a "logic-level" gate threshold, since
the NPN stage already provides a large `Vgs` swing regardless of the
Vault's own 3.3 V logic level. A dedicated load-switch IC (e.g. TI
TPS22860, ON Semi NCP380) replaces this whole discrete circuit with a
single part wired directly to the rail-enable GPIO, and is worth
considering once you move past a bench prototype — some also add
soft-start, limiting the inrush current spike when your MCU's decoupling
capacitors first charge.

---

## 3. I2C register map

Access pattern: the master writes a single **register address byte** to
select a register, then either writes data bytes (which apply to that
register, starting at byte offset 0) or performs a repeated-START read to
pull data back from it. **Register addresses are fixed** — the same on every
Vault hardware variant, regardless of how large its context buffer is.
Auto-increment during a multi-byte transfer stays **within the selected
register only** — it never rolls into the next register number, so you
cannot accidentally overwrite `COMMAND` by writing one byte too many to
`CONTEXT_DATA`.

| Addr | Name | Access | Size | Meaning |
|---|---|---|---|---|
| `0x00` | `STATUS` | R | 1 B | bit0 = `CONTEXT_VALID` (see §3.1). Bits 1-7 reserved, always read 0. |
| `0x01` | `PROTOCOL_VERSION` | R | 1 B | Currently `0x01`. Check this before trusting the rest of the map — a future Vault firmware revision would bump this if the register layout ever changes. |
| `0x02` | `CONTEXT_LENGTH` | R/W | 1 B | How many bytes of `CONTEXT_DATA` are actually meaningful. You set this when writing a new context. |
| `0x03` | `CONTEXT_DATA` | R/W | up to `VAULT_CONTEXT_SIZE` (see §3.2) | Your opaque LoRaWAN session blob. The Vault never parses it. |
| `0x04` | `COMMAND` | W | 1 B | Write `0x01` (`CMD_DONE`) when you're finished — see §4. |
| `0x05` | `WAKE_INTERVAL_SEC` | R/W | 4 B, little-endian `uint32_t` | Seconds until the *next* wake. You own this value — see §3.3. |

### 3.1 `STATUS` / `CONTEXT_VALID`

`CONTEXT_VALID` is `0` on a truly first-ever boot (factory-fresh Vault,
nothing has ever been written) and on any later boot where the Vault's own
supply was fully lost (e.g. its backup battery was swapped) before you had a
chance to write a context. It becomes `1` the moment you successfully
finish writing a `CONTEXT_DATA` transaction (i.e. it flips at your `STOP`
condition, not on your first byte — a transfer you abandon partway through
never marks a half-written buffer as valid).

**This bit is your join/rejoin decision point** — see §5.

### 3.2 `CONTEXT_DATA` sizing

`VAULT_CONTEXT_SIZE` is a compile-time constant baked into the Vault
firmware for its specific MCU, **not** something you can query over I2C —
you need to know it out-of-band, from whichever Vault variant you're
integrating with:

| Vault MCU | `VAULT_CONTEXT_SIZE` |
|---|---|
| LPC810 | 320 bytes |
| STM32U031F8P6 | 128 bytes |

This matters because LoRaWAN 1.0.x and 1.1 sessions are different sizes:

| LoRaWAN version | Minimum session footprint | Fits in LPC810's 320 B? | Fits in STM32U031F8P6's 128 B? |
|---|---|---|---|
| 1.0.x | 40 B (DevAddr, NwkSKey, AppSKey, FCntUp) | Yes | Yes |
| 1.1 | 80 B (DevAddr, FNwkSIntKey, SNwkSIntKey, NwkSEncKey, AppSKey, FCntUp, NFCntDown, AFCntDown) | Yes | Yes |

**Where the LPC810's 320-byte figure comes from:** it's sized to
[RadioLib](https://github.com/jgromes/RadioLib)'s actual persisted-state
requirement, not a round number — `RADIOLIB_LORAWAN_NONCES_BUF_SIZE` (14
bytes) + `RADIOLIB_LORAWAN_SESSION_BUF_SIZE` (302 bytes) = 316, rounded up.
If you're using a different LoRaWAN stack, check its own persisted-state
size the same way — a full-featured stack like LoRaMAC-node tracking ADR
parameters, channel masks, and link-check tokens can run 256–512+ bytes,
which may or may not fit depending on which Vault variant you're
targeting and how much of that state you actually need to persist across
sleep. If it doesn't fit, store only the fields you need to skip a
re-join (the LoRaWAN-version table above) and let your stack
recompute/re-negotiate the rest after each wake.

If you write more bytes than `VAULT_CONTEXT_SIZE` in one `CONTEXT_DATA`
transaction, the extra bytes are silently dropped — not an error, but not
stored either. Keep your writes within the size table above.

### 3.3 `WAKE_INTERVAL_SEC` ownership

The Vault does not have an opinion on how often you should report — that's
your application's business logic ("send every 5 minutes", "send every hour
overnight, every 10 minutes during the day", etc). You decide the interval
and write it; the Vault's job is purely to enforce whatever value is
currently set, autonomously, with its own timer, since it's the only thing
awake between your sessions.

- You may write a new value on any wake cycle, or leave it unchanged from
  the previous cycle (it persists across the Vault's own sleep — it's only
  reset to a firmware default if the *Vault's own* battery is fully lost,
  which is a distinct, rarer event from your MCU's power being cut every
  cycle by design).
- If you never write it (e.g. your very first-ever boot, before you've
  decided on a reporting cadence), the Vault uses its own compiled-in
  default.
- Reading it back tells you what will actually be used for the next sleep —
  useful for confirming a write took effect before you finish your session.

---

## 4. The wake-cycle protocol, from your side

Every cycle looks like this from your MCU's perspective:

1. **You boot from reset** (the Vault just powered you on). This is your
   only wake signal — there's no interrupt line, no message, nothing else
   to check first.
2. **I2C is live immediately.** The Vault is already acting as an I2C slave
   the moment your rail comes up — you can start talking to it as soon as
   your own I2C peripheral is initialized.
3. **Do your work**, in any order you like: read `STATUS`/`PROTOCOL_VERSION`
   to check state, read/write `CONTEXT_DATA` + `CONTEXT_LENGTH`, read/write
   `WAKE_INTERVAL_SEC`, run your sensor measurement and LoRaWAN transmission
   in between.
4. **Write your final, up-to-date session state to `CONTEXT_DATA`/
   `CONTEXT_LENGTH`** — do this *before* the next step, since once you
   signal done, the Vault will cut your power and nothing you do after that
   point can be saved.
5. **Write `0x01` to `COMMAND`.** This tells the Vault you're finished.
6. **Stop touching the I2C bus.** Don't start another transaction after
   `COMMAND`. The Vault acts on `CMD_DONE` once your `STOP` condition
   completes — after that, expect your power to be cut at any moment. There
   is nothing further for your firmware to do to "shut down": you don't
   need to power yourself off, de-init peripherals, or enter your own sleep
   mode for the Vault's sake. As a defensive habit, though, it's reasonable
   to have your main loop simply halt/spin (or enter your own lowest-power
   wait state) immediately after writing `CMD_DONE`, purely as a safety net
   in case power removal is delayed for any reason on a given board — not
   because the protocol requires it.

The Vault, on its side: turns your rail off, puts the I2C pins into
high-impedance (§2.1), arms its wake timer for whatever `WAKE_INTERVAL_SEC`
currently holds, and sleeps until that timer fires — at which point step 1
happens again.

---

## 5. Worked example: an OTAA sensor

This walks through exactly the scenario most integrators hit first: a
battery/solar sensor node that joins the network via OTAA once, then
reports periodically without rejoining every cycle.

### 5.1 Flow

```
BOOT (rail just powered by Vault)
  |
  v
Init your I2C peripheral as master, talk to Vault at 0x42
  |
  v
Read STATUS
  |
  +-- CONTEXT_VALID == 0 (first-ever boot, or Vault battery was replaced)
  |     |
  |     v
  |   Perform OTAA join over the radio (DevEUI/AppEUI/AppKey are baked into
  |   your firmware or provisioned separately -- they are NOT stored in the
  |   Vault; only the SESSION that results from a successful join is)
  |     |
  |     v
  |   Join accepted -> you now have DevAddr, session keys, FCntUp=0
  |
  +-- CONTEXT_VALID == 1 (normal wake -- already joined previously)
        |
        v
      Read CONTEXT_LENGTH, then read CONTEXT_DATA for that many bytes
        |
        v
      Restore DevAddr / session keys / frame counters into your LoRaWAN
      stack's session state -- no OTAA join needed
        |
        v
  (both paths converge here)
        |
        v
Take your sensor measurement
        |
        v
Send the LoRaWAN uplink (this increments FCntUp in your stack's own state)
        |
        v
Write your CURRENT session state back:
  - write CONTEXT_LENGTH  (e.g. 40 for LoRaWAN 1.0.x)
  - write CONTEXT_DATA    (DevAddr, keys, updated FCntUp)
        |
        v
(optional) write WAKE_INTERVAL_SEC if you want to change your reporting
cadence from what was already set
        |
        v
Write COMMAND = 0x01 (CMD_DONE)
        |
        v
Stop touching the bus; wait for power to be cut
        |
        v
  <-- Vault cuts your rail, arms its timer, sleeps -->
        |
        v
(WAKE_INTERVAL_SEC later) Vault powers your rail back on -> back to BOOT
```

### 5.2 Example master-side code (Arduino sketch)

This is a complete `.ino` sketch using Arduino's built-in `Wire` library for
the I2C master side. The LoRaWAN stack calls (`lorawanOtaaJoin()`,
`lorawanRestoreSession()`, etc.) are left as stubs you fill in against
whichever library your board uses — e.g. **MCCI LoRaWAN LMIC** (`MCCI_LoRaWAN_LMIC_library`)
or **RadioLib** — since the exact session-export/import API differs between
them. The Vault-side logic (the part this guide is actually about) is
complete and ready to use as-is.

Note the fit with Arduino's own program structure: since this device
reboots fresh on every wake (the Vault re-powers it from reset each cycle,
per §4), **the entire cycle fits naturally into `setup()`**, and `loop()`
is intentionally left empty — there's nothing to repeat, since a whole new
`setup()` only happens after the next wake.

```cpp
#include <Wire.h>

// ---- Vault register map (see section 3) ----
constexpr uint8_t VAULT_I2C_ADDR              = 0x42;

constexpr uint8_t VAULT_REG_STATUS            = 0x00;
constexpr uint8_t VAULT_REG_PROTOCOL_VERSION  = 0x01;
constexpr uint8_t VAULT_REG_CONTEXT_LENGTH    = 0x02;
constexpr uint8_t VAULT_REG_CONTEXT_DATA      = 0x03;
constexpr uint8_t VAULT_REG_COMMAND           = 0x04;
constexpr uint8_t VAULT_REG_WAKE_INTERVAL_SEC = 0x05;

constexpr uint8_t VAULT_CMD_DONE              = 0x01;
constexpr uint8_t VAULT_STATUS_CONTEXT_VALID_BIT = (1u << 0);

// LoRaWAN 1.0.x session layout -- adjust for 1.1 if your stack uses it and
// your Vault variant's VAULT_CONTEXT_SIZE has room (see section 3.2).
struct LoRaWANSession {
    uint8_t  devAddr[4];
    uint8_t  nwkSKey[16];
    uint8_t  appSKey[16];
    uint32_t fcntUp;
};

constexpr uint8_t LORAWAN_SESSION_SIZE = 40; // 4 + 16 + 16 + 4

// ---- Vault I2C helpers ----

uint8_t vaultReadReg(uint8_t reg) {
    Wire.beginTransmission(VAULT_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false); // repeated START, keep the bus held
    Wire.requestFrom(VAULT_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void vaultWriteReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(VAULT_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

void vaultReadContext(uint8_t *out, uint8_t len) {
    Wire.beginTransmission(VAULT_I2C_ADDR);
    Wire.write(VAULT_REG_CONTEXT_DATA);
    Wire.endTransmission(false);
    Wire.requestFrom(VAULT_I2C_ADDR, len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) {
        out[i] = Wire.read();
    }
}

void vaultWriteContext(const uint8_t *data, uint8_t len) {
    Wire.beginTransmission(VAULT_I2C_ADDR);
    Wire.write(VAULT_REG_CONTEXT_DATA);
    Wire.write(data, len);
    Wire.endTransmission();
}

void vaultWriteWakeIntervalSec(uint32_t seconds) {
    Wire.beginTransmission(VAULT_I2C_ADDR);
    Wire.write(VAULT_REG_WAKE_INTERVAL_SEC);
    Wire.write((uint8_t)(seconds & 0xFF));
    Wire.write((uint8_t)((seconds >> 8) & 0xFF));
    Wire.write((uint8_t)((seconds >> 16) & 0xFF));
    Wire.write((uint8_t)((seconds >> 24) & 0xFF));
    Wire.endTransmission();
}

// ---- Session <-> byte-buffer packing ----

void packSession(const LoRaWANSession &s, uint8_t *out) {
    memcpy(&out[0], s.devAddr, 4);
    memcpy(&out[4], s.nwkSKey, 16);
    memcpy(&out[20], s.appSKey, 16);
    out[36] = (uint8_t)(s.fcntUp & 0xFF);
    out[37] = (uint8_t)((s.fcntUp >> 8) & 0xFF);
    out[38] = (uint8_t)((s.fcntUp >> 16) & 0xFF);
    out[39] = (uint8_t)((s.fcntUp >> 24) & 0xFF);
}

void unpackSession(const uint8_t *in, LoRaWANSession &s) {
    memcpy(s.devAddr, &in[0], 4);
    memcpy(s.nwkSKey, &in[4], 16);
    memcpy(s.appSKey, &in[20], 16);
    s.fcntUp = (uint32_t)in[36] | ((uint32_t)in[37] << 8) |
               ((uint32_t)in[38] << 16) | ((uint32_t)in[39] << 24);
}

// ---- Stubs: fill these in against your LoRaWAN library of choice
//      (MCCI LMIC, RadioLib, etc.) ----

void lorawanOtaaJoin();                                  // blocks until joined
void lorawanRestoreSession(const LoRaWANSession &s);
void lorawanExportSession(LoRaWANSession &s);             // pulls current keys/fcnt out of the stack
void lorawanSendUplink(float measurement);
float readSensor();

void setup() {
    Wire.begin(); // join the I2C bus as master

    LoRaWANSession session;

    uint8_t status = vaultReadReg(VAULT_REG_STATUS);

    if (status & VAULT_STATUS_CONTEXT_VALID_BIT) {
        // Normal wake: restore the session the Vault already had.
        uint8_t raw[LORAWAN_SESSION_SIZE];
        uint8_t len = vaultReadReg(VAULT_REG_CONTEXT_LENGTH);
        vaultReadContext(raw, len);
        unpackSession(raw, session);
        lorawanRestoreSession(session);
    } else {
        // First-ever boot (or the Vault's own battery was replaced):
        // no stored session, must OTAA join before sending anything.
        lorawanOtaaJoin();
        lorawanExportSession(session);
    }

    lorawanSendUplink(readSensor()); // this increments fcntUp inside your stack
    lorawanExportSession(session);   // re-export: fcntUp has changed

    uint8_t raw[LORAWAN_SESSION_SIZE];
    packSession(session, raw);
    vaultWriteReg(VAULT_REG_CONTEXT_LENGTH, LORAWAN_SESSION_SIZE);
    vaultWriteContext(raw, LORAWAN_SESSION_SIZE);

    vaultWriteWakeIntervalSec(300); // report again in 5 minutes

    vaultWriteReg(VAULT_REG_COMMAND, VAULT_CMD_DONE);

    // Power will be cut shortly; nothing left to do. loop() stays empty
    // on purpose -- see the note above this sketch.
}

void loop() {
    // Intentionally empty: this device only ever runs setup() once per
    // wake cycle. See section 4, step 6.
}
```

### 5.3 Notes on the OTAA join itself

The Vault has no involvement in the join procedure — DevEUI, AppEUI/JoinEUI,
and AppKey are your device's provisioning credentials and live in your own
firmware/secure element, never in the Vault. The Vault only ever sees the
**result** of a join (DevAddr + session keys + counters), stored as an
opaque blob it doesn't parse. If a join attempt fails, simply don't write
`CONTEXT_DATA` — `CONTEXT_VALID` stays `0`, and your next wake will
correctly retry the join rather than trying to restore a session that was
never established.

### 5.4 ABP alternative

If your device uses ABP (a pre-provisioned DevAddr + keys, no OTAA) instead
of OTAA, the flow simplifies: on `CONTEXT_VALID == 0`, write your
pre-provisioned session directly to `CONTEXT_DATA` instead of performing a
join, then proceed exactly as in the normal-wake path from that point on.

---

## 6. Reference

- Register semantics and rationale: [docs/superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md](superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md), section 5.
- Hardware selection rationale (why these specific MCUs, sleep-current
  figures): [docs/MCU_Analysis_Report.md](MCU_Analysis_Report.md).
- Build/flash instructions for the Vault firmware itself: [README.md](../README.md).
