#include <stdbool.h>

#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "LPC8xx.h"

/* SWM movable-function pin assignment for I2C0 SDA/SCL. Per the vendored
   header, I2C_SDA and I2C_SCL are movable functions (PINASSIGN7 bits
   [31:24] and PINASSIGN8 bits [7:0] respectively), not fixed-function
   pins on this part -- confirmed by the SWM_PINASSIGN7_I2C_SDA_IO_MASK /
   SWM_PINASSIGN8_I2C_SCL_IO_MASK field names in LPC8xx.h. Still verify
   the pin routing (PIO0_10/PIO0_11) against the UM10601 "Switch Matrix"
   chapter and the board schematic before flashing. */
#define I2C_SDA_PIN 10u /* PIO0_10 -- verify against schematic */
#define I2C_SCL_PIN 11u /* PIO0_11 -- verify against schematic */

/* NXP SWM convention: a movable-function PINASSIGN byte field of 0xFF
   means "not assigned to any pin" -- verify this against UM10601 before
   relying on it to fully undo the assignment in platform_i2c_slave_deinit(). */
#define SWM_PINASSIGN_UNASSIGNED 0xFFu

static void lpc810_i2c_pins_to_i2c_function(void) {
    SYSCON->SYSAHBCLKCTRL |= SYSCON_SYSAHBCLKCTRL_SWM_MASK;

    SWM0->PINASSIGN7 = (SWM0->PINASSIGN7 & ~SWM_PINASSIGN7_I2C_SDA_IO_MASK) |
                        SWM_PINASSIGN7_I2C_SDA_IO(I2C_SDA_PIN);
    SWM0->PINASSIGN8 = (SWM0->PINASSIGN8 & ~SWM_PINASSIGN8_I2C_SCL_IO_MASK) |
                        SWM_PINASSIGN8_I2C_SCL_IO(I2C_SCL_PIN);
}

void platform_i2c_slave_init(uint8_t addr) {
    SYSCON->SYSAHBCLKCTRL |= SYSCON_SYSAHBCLKCTRL_I2C0_MASK;
    lpc810_i2c_pins_to_i2c_function();

    /* Slave Address 0 register: 7-bit address in SLVADR_SLVADR (bits
       [7:1]), bit 0 is SADISABLE (0 = address enabled, 1 = disabled) --
       verify this field layout, and in particular the SADISABLE polarity,
       against UM10601 "Slave Address 0 register" before flashing. */
    I2C0->SLVADR[0] = I2C_SLVADR_SLVADR(addr);

    /* Enable I2C0 slave function and its interrupt. Verify CFG.SLVEN bit
       position and confirm I2C0_IRQn's value in LPC8xx.h matches the
       vector table position in startup_lpc810.c, per the note left there
       in Task 6. */
    I2C0->CFG |= I2C_CFG_SLVEN_MASK;
    I2C0->INTENSET |= I2C_INTENSET_SLVPENDINGEN_MASK | I2C_INTENSET_SLVDESELEN_MASK;

    NVIC_EnableIRQ(I2C0_IRQn);
}

void platform_i2c_slave_deinit(void) {
    NVIC_DisableIRQ(I2C0_IRQn);
    I2C0->CFG &= ~I2C_CFG_SLVEN_MASK;
    I2C0->INTENCLR |= I2C_INTENCLR_SLVPENDINGCLR_MASK;
    SYSCON->SYSAHBCLKCTRL &= ~SYSCON_SYSAHBCLKCTRL_I2C0_MASK;

    /* Undo the SWM movable-function assignment (see SWM_PINASSIGN_UNASSIGNED
       comment above); platform_bus_isolate() (Task 7) then sets the pins to
       plain GPIO input with no pulls. Verify the "unassign" encoding against
       UM10601 before relying on it. */
    SWM0->PINASSIGN7 |= SWM_PINASSIGN7_I2C_SDA_IO_MASK;
    SWM0->PINASSIGN8 |= SWM_PINASSIGN8_I2C_SCL_IO_MASK;
}

void I2C0_IRQHandler(void) {
    /* Verify this whole handler's state-decoding logic against UM10601
       "I2C slave state codes" (STAT.SLVSTATE encodes ADDR/RX/TX; this
       sketch assumes 0=ADDR, 1=RX, 2=TX, matching the commonly documented
       LPC81x encoding, but confirm before relying on it). */
    uint32_t stat = I2C0->STAT;

    /* STAT.SLVDESEL (bit 15) is a real, dedicated slave-deselect condition
       -- distinct from SLVPENDING/SLVSTATE -- that the LPC81x I2C0
       peripheral sets whenever the slave is deselected, which on this
       part happens both on a bus STOP and on a repeated START to a
       different address. This is the mechanism the earlier
       s_transaction_in_progress heuristic (Task 9) was working around
       because it hadn't been found yet; it supersedes that heuristic
       entirely; the last transaction of a wake cycle (the CMD_DONE
       write) now reliably promotes s_pending_done via
       vault_i2c_registers_on_stop() even when the master powers down
       immediately afterward and no subsequent address match ever
       arrives. Enabled via I2C_INTENSET_SLVDESELEN in
       platform_i2c_slave_init(). STAT is documented (UM10601 "I2C status
       register") as write-1-to-clear for this bit; verify that against
       the manual before flashing, along with whether SLVDESEL can arrive
       standalone (without SLVPENDING also set) -- this handler assumes
       it can, and checks for it unconditionally rather than nesting it
       under the SLVPENDING branch below. */
    if (stat & I2C_STAT_SLVDESEL_MASK) {
        I2C0->STAT = I2C_STAT_SLVDESEL_MASK; /* write-1-to-clear -- verify against UM10601 */
        vault_i2c_registers_on_stop();
    }

    if (!(stat & I2C_STAT_SLVPENDING_MASK)) {
        return;
    }

    uint32_t slvstate = (stat & I2C_STAT_SLVSTATE_MASK) >> I2C_STAT_SLVSTATE_SHIFT;

    switch (slvstate) {
    case 0u: /* address match */
        (void)I2C0->SLVDAT; /* clears the address-match condition on some parts -- verify */
        break;
    case 1u: /* slave receive: master is writing a byte to us */
        vault_i2c_registers_on_write_byte((uint8_t)I2C0->SLVDAT);
        break;
    case 2u: /* slave transmit: master is reading a byte from us */
        I2C0->SLVDAT = vault_i2c_registers_on_read_request();
        break;
    default:
        break;
    }

    I2C0->SLVCTL |= I2C_SLVCTL_SLVCONTINUE_MASK;
}
