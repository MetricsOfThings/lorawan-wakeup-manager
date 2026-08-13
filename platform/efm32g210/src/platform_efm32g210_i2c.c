#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_i2c.h"
#include "efm32g210_pins.h"

/* PD6 (SDA)/PD7 (SCL) -- shared with platform_efm32g210_gpio.c via
   efm32g210_pins.h; see that header's comment for why the two files
   must be kept in lockstep. */
#define I2C_SDA_PORT EFM32G210_I2C_SDA_PORT
#define I2C_SDA_PIN  EFM32G210_I2C_SDA_PIN
#define I2C_SCL_PORT EFM32G210_I2C_SCL_PORT
#define I2C_SCL_PIN  EFM32G210_I2C_SCL_PIN

/* I2C0 ROUTE LOCATION for PD6 (SDA)/PD7 (SCL).
 *
 * Verified against the real vendored headers, not guessed:
 *   vendor/Gecko_SDK/platform/Device/SiliconLabs/EFM32G/Include/efm32g_af_pins.h:
 *     AF_I2C0_SDA_PIN(i)  = i==0:0  i==1:6  i==2:6  i==3:14
 *     AF_I2C0_SCL_PIN(i)  = i==0:1  i==1:7  i==2:7  i==3:15
 *   efm32g_af_ports.h:
 *     AF_I2C0_SDA_PORT(i) = i==0:0(A) i==1:3(D) i==2:2(C) i==3:3(D)
 *     AF_I2C0_SCL_PORT(i) = i==0:0(A) i==1:3(D) i==2:2(C) i==3:3(D)
 * Location index 1 is the only one that yields port D (gpioPortD == 3,
 * per em_gpio.h's GPIO_Port_TypeDef) pins 6/7 -- i.e. PD6/PD7 -- matching
 * this board's I2C0_SDA/I2C0_SCL silkscreen labels. The brief's guessed
 * LOCATION 0 (LOC0) actually maps to PA0/PA1, which is wrong for this
 * board; LOC1 is the correct value, confirmed against
 * efm32g210f128.h's I2C_ROUTE_LOCATION_LOC1 macro. */
#define I2C0_ROUTE_LOCATION I2C_ROUTE_LOCATION_LOC1

/* Bus speed: the design spec (§6) targets 400 kHz Fast-mode, matching
   both other backends. In slave mode this peripheral doesn't generate
   SCL -- the master does -- so there is no baud/timing register to set
   here the way a master-mode driver would need. What does matter is
   whether this slave's own sampling/digital-filter clock (I2C0's
   peripheral clock, sourced from HFPERCLK, itself derived from HFCLK --
   the 32 MHz HFXO per efm32g210_clock_init()) is fast enough to reliably
   track a 400 kHz master's SCL/SDA edges. 32 MHz is 80x the 400 kHz bus
   rate, which is comfortably oversampled by the rule-of-thumb margins
   platform_lpc810_i2c.c's CLKDIV comment applies to its own slave (30x
   at 12 MHz there). However, unlike that LPC810 driver's CLKDIV (an
   explicit, documented "must be fast enough" divisor), this Series-0
   EFM32G210 I2C peripheral's real minimum-peripheral-clock-vs-SCL-rate
   requirement for reliable Fast-mode slave tracking is not stated
   anywhere in the vendored em_i2c.h/efm32g210f128.h comments this task
   had access to, and em_i2c.c's own I2C_BusFreqSet() is master-mode-only
   (computes CLKDIV from a target SCL frequency, which this slave-mode
   driver never calls). Rather than assert a specific margin number this
   task cannot verify from the vendored headers alone, this is flagged
   as an unverified hardware bring-up item: confirm actual 400 kHz
   Fast-mode slave tracking with a scope/logic analyzer during Task 9
   (EFM32G210 hardware bring-up verification) before assuming this
   configuration meets the design spec's target. */
void platform_i2c_slave_init(uint8_t addr) {
    CMU_ClockEnable(cmuClock_I2C0, true);

    /* Route SDA/SCL to PD6/PD7 and enable the pins in the ROUTE
       register. Field names (I2C_ROUTE_SDAPEN, I2C_ROUTE_SCLPEN,
       I2C_ROUTE_LOCATION_LOCn) confirmed against efm32g210f128.h. */
    I2C0->ROUTE = I2C_ROUTE_SDAPEN | I2C_ROUTE_SCLPEN | I2C0_ROUTE_LOCATION;

    /* Reconfigure PD6/PD7 from the high-impedance isolation state
       platform_bus_isolate() (Task 3) leaves them in, into the I2C
       peripheral's required open-drain-with-pull-up mode
       (gpioModeWiredAndPullUp, confirmed in em_gpio.h's
       GPIO_Mode_TypeDef -- I2C is a wired-AND/open-drain bus, it must
       never be driven push-pull). `out`=1 so DOUT is high (released,
       i.e. not actively pulling the line low) when the pin isn't
       actively clocking/ack'ing low. */
    GPIO_PinModeSet(I2C_SDA_PORT, I2C_SDA_PIN, gpioModeWiredAndPullUp, 1);
    GPIO_PinModeSet(I2C_SCL_PORT, I2C_SCL_PIN, gpioModeWiredAndPullUp, 1);

    /* Slave address in bits [7:1] -- confirmed against efm32g210f128.h's
       I2C_SADDR bitfields (_I2C_SADDR_ADDR_SHIFT == 1,
       _I2C_SADDR_ADDR_MASK == 0xFE), matching the brief's shift. */
    I2C0->SADDR = (uint32_t)(addr << 1);

    /* CLKDIV: the vendored em_i2c.c (I2C_BusFreqSet) carries an explicit
       comment -- "The clock divisor must be at least 1 in slave mode
       according to the reference manual" -- and forces div=1 whenever
       CTRL.SLAVE is set and the computed divisor would otherwise be 0.
       This driver never calls I2C_BusFreqSet (no master-mode frequency
       to compute; slave mode tracks the master's clock), but CLKDIV
       still resets to 0, which is exactly the value that comment says is
       invalid for slave mode -- so it must be set explicitly here rather
       than left at its reset default. */
    I2C0->CLKDIV = 1;

    /* Clear any stale flags before enabling, matching the order emlib's
       own I2C_Init() uses (IEN=0 + IntClear before touching CTRL/state),
       so a leftover flag from a previous init/deinit cycle can't
       immediately re-trigger the IRQ once enabled below. */
    I2C0->IFC = _I2C_IFC_MASK;

    /* Enable the peripheral in SLAVE mode. Deliberately NOT using
     * I2C_CTRL_AUTOACK ("Automatic Acknowledge") -- an earlier version of
     * this driver did, and it caused intermittent, silent I2C data
     * corruption on real hardware (bytes occasionally wrong on the master
     * side with no error reported, both for register reads like
     * VAULT_REG_VERSION and for context writes). Root cause: AUTOACK
     * makes hardware ACK every received byte the instant it arrives, with
     * no clock-stretching tied to firmware actually being ready --
     * clock-stretching only happens for firmware-timing-independent
     * reasons (e.g. internal sync), not to wait for RXDATA to be read. If
     * this ISR is ever so much as one byte-time late (interrupt latency,
     * a slower vault_i2c_registers_on_write_byte() call, IRQs briefly
     * masked elsewhere), the next incoming byte silently overwrites
     * RXDATA before it's consumed -- no error, no NACK, just a dropped/
     * corrupted byte. This is the same hazard class the LPC810 backend's
     * platform_lpc810_i2c.c avoids structurally: that peripheral holds
     * SCL low (SLVPENDING) until firmware writes SLVCONTINUE, so the
     * master physically cannot send the next byte before firmware is
     * ready. Without AUTOACK, this part provides the equivalent guarantee
     * via manual ACK: the peripheral clock-stretches (holds SCL low)
     * after every address match and every received data byte, waiting
     * for firmware to explicitly write I2C_CMD_ACK (or NACK) before
     * continuing -- see I2C0_IRQHandler below, which now writes CMD.ACK
     * after consuming each ADDR/RXDATAV byte, closing exactly the window
     * AUTOACK left open. I2C_CTRL_SLAVE (bit 1, "Addressable as Slave")
     * is still required and unrelated to this: without it the peripheral
     * does not respond to its own slave address at all -- em_i2c.c's own
     * I2C_Init() sets it via
     * BUS_RegBitWrite(&i2c->CTRL, _I2C_CTRL_SLAVE_SHIFT, !init->master). */
    I2C0->CTRL |= I2C_CTRL_SLAVE | I2C_CTRL_EN;

    /* Enable the relevant interrupt sources: address match, RX data
       valid, and slave STOP. Field names (I2C_IEN_ADDR, I2C_IEN_RXDATAV,
       I2C_IEN_TXBL, I2C_IEN_SSTOP) confirmed against efm32g210f128.h.

       I2C_IEN_TXBL is deliberately NOT included here. TXBL is a
       level-sensitive status flag, not an edge/event flag: it reflects
       "TX buffer has room" and is asserted at reset (_I2C_IF_RESETVALUE
       has bit 4 set, confirmed in efm32g_i2c.h) and is NOT clearable via
       IFC (_I2C_IFC_MASK excludes bit 4). The only way to deassert it is
       to write TXDATA. That combination means TXBL cannot be enabled in
       IEN unconditionally at init: if it were, it would be pending the
       instant NVIC_EnableIRQ(I2C0_IRQn) runs (bus idle, nothing to
       transmit), and because nothing here ever writes TXDATA while
       idle, the flag would never clear -- I2C0_IRQHandler would
       tail-chain forever at full CPU speed, defeating
       platform_wait_for_interrupt()'s __WFI() (every WFI would return
       immediately because an interrupt is permanently pending) and
       burning power for as long as the peripheral stays enabled.

       Instead, I2C_IEN_TXBL is enabled/disabled dynamically by
       I2C0_IRQHandler itself, bracketing exactly the window where the
       peripheral is genuinely an addressed transmitter: enabled in the
       ADDR branch once I2C0->STATE's TRANSMITTER bit confirms a master
       read has actually started (so TXBL only becomes pending once
       there is a real TXDATA write coming to clear it), and disabled
       again in the SSTOP branch once the transaction completes. */
    I2C0->IEN = I2C_IEN_ADDR | I2C_IEN_RXDATAV | I2C_IEN_SSTOP;

    NVIC_EnableIRQ(I2C0_IRQn);
}

void platform_i2c_slave_deinit(void) {
    NVIC_DisableIRQ(I2C0_IRQn);
    I2C0->IEN = 0;
    I2C0->CTRL &= ~(I2C_CTRL_EN | I2C_CTRL_SLAVE | I2C_CTRL_AUTOACK);
    I2C0->ROUTE = 0;
    CMU_ClockEnable(cmuClock_I2C0, false);
}

void platform_wait_for_interrupt(void) {
    /* platform_enter_low_power_sleep() (platform_efm32g210.c, Task 6)
       calls EMU_EnterEM2(), which sets SCB->SCR's SLEEPDEEP bit and
       never clears it -- confirmed directly in the vendored
       vendor/Gecko_SDK/platform/emlib/src/em_emu.c. Without explicitly
       clearing it here, any call to this function after the first full
       EM2 sleep cycle would silently fall through to EM2 deep sleep
       instead of plain Sleep mode (CPU clock only, everything else --
       including I2C0 -- still running and able to service the interrupt
       this is waiting for). Plain __WFI() with SLEEPDEEP clear halts
       only the CPU core clock until any enabled interrupt fires. Same
       fix, same hazard class, as platform_lpc810_power.c's and
       platform_stm32u031.c's platform_wait_for_interrupt(). */
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
}

/* See vault/platform.h's doc comment for why these exist (closing a
   lost-wakeup race around vault_core's WFI-based I2C wait loop). Plain
   ARM CMSIS intrinsics -- WFI still wakes on a pending-but-masked
   interrupt, per the ARM architecture. */
void platform_irq_disable(void) {
    __disable_irq();
}

void platform_irq_enable(void) {
    __enable_irq();
}

void I2C0_IRQHandler(void) {
    uint32_t flags = I2C0->IF;

    /* Decode order confirmed against efm32g210f128.h's I2C_IF bit
       layout: ADDR (bit 2), TXC (3), TXBL (4), RXDATAV (5), ACK (6),
       NACK (7), SSTOP (16) are all independently testable status bits,
       matching the general EFM32 I2C IF register convention the brief
       assumed. AUTOACK is deliberately NOT set (see
       platform_i2c_slave_init()'s comment) -- this handler explicitly
       writes I2C_CMD_ACK after consuming the ADDR and RXDATAV bytes
       below, which is what gives real clock-stretching instead of
       AUTOACK's fire-and-forget behavior.

       On this part, ADDR and RXDATAV assert *together* on an address
       match, and the matched address byte is pushed into RXDATA just
       like an ordinary received data byte -- it must be explicitly read
       out to consume it, the same way platform_lpc810_i2c.c's
       I2C0_IRQHandler does `(void)I2C0->SLVDAT;` on its own
       address-match case. The ADDR branch below reads RXDATA to consume
       that byte; the `else if` (not a separate `if`) is deliberate --
       `flags` is a snapshot taken at entry, so without the `else` the
       RXDATAV branch would still see RXDATAV set from that same
       snapshot and read RXDATA a second time, consuming the *next*
       byte (or stale/garbage data) instead of leaving it for the
       following interrupt. On a genuine data-byte interrupt (no
       concurrent address match), ADDR is clear and this correctly falls
       through to the RXDATAV branch as before. */
    if (flags & I2C_IF_ADDR) {
        (void)I2C0->RXDATA; /* consume the matched address byte */
        I2C0->IFC = I2C_IFC_ADDR;

        /* Without AUTOACK (see platform_i2c_slave_init()'s comment), the
           peripheral clock-stretches after the address match until
           firmware acks it -- this releases that stretch now that the
           address byte has actually been consumed above. I2C_CMD is a
           write-only strobe register (confirmed in efm32g_i2c.h: no
           readback/read-modify-write needed), so writing just this bit
           doesn't disturb anything else. */
        I2C0->CMD = I2C_CMD_ACK;

        /* A master READ addresses this slave as transmitter --
           I2C0->STATE's TRANSMITTER bit (confirmed in efm32g_i2c.h)
           reflects this immediately after the address match. Only now,
           with a real transaction underway that will shortly write
           TXDATA (clearing TXBL -- see platform_i2c_slave_init()'s
           I2C_IEN_TXBL comment), is it safe to enable I2C_IEN_TXBL: the
           flag is level-sensitive and already pending the moment
           there's room in TXDATA, so enabling it here immediately
           raises the interrupt for the first byte, as intended. */
        if (I2C0->STATE & I2C_STATE_TRANSMITTER) {
            I2C0->IEN |= I2C_IEN_TXBL;
        } else {
            /* A repeated START into a WRITE phase (read, then repeated
               START, then write) re-enters this branch with TRANSMITTER
               now clear -- without this else, TXBL would stay armed
               from the prior read phase and storm for the whole write
               phase, since a write never touches TXDATA to clear it. */
            I2C0->IEN &= ~I2C_IEN_TXBL;
        }
    } else if (flags & I2C_IF_RXDATAV) {
        vault_i2c_registers_on_write_byte((uint8_t)I2C0->RXDATA);

        /* Same clock-stretch release as the ADDR branch above, but for a
           data byte: only ack now that the byte has actually been
           consumed by vault_i2c_registers_on_write_byte(), so the master
           physically cannot start clocking the next byte in before this
           one has been processed. This is the fix for the AUTOACK data
           race described in platform_i2c_slave_init()'s comment. */
        I2C0->CMD = I2C_CMD_ACK;
    }

    /* Belt-and-braces: even though I2C_IEN_TXBL is now only enabled
       while STATE.TRANSMITTER is set (see the ADDR branch above), gate
       on it here too before acting on TXBL, since it's a level-sensitive
       status flag that could in principle still be pending across a
       state transition within the same IF snapshot. I2C_STATE_TRANSMITTER
       confirmed in efm32g_i2c.h. */
    if ((flags & I2C_IF_TXBL) && (I2C0->STATE & I2C_STATE_TRANSMITTER)) {
        I2C0->TXDATA = vault_i2c_registers_on_read_request();
    }

    if (flags & I2C_IF_SSTOP) {
        I2C0->IFC = I2C_IFC_SSTOP;
        vault_i2c_registers_on_stop();

        /* Transaction is over -- disable TXBL again so it doesn't stay
           armed (and thus storming, per the I2C_IEN_TXBL comment in
           platform_i2c_slave_init()) until the next master read starts
           it back up via the ADDR branch above. */
        I2C0->IEN &= ~I2C_IEN_TXBL;
    }
}
