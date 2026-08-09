#include "vault/platform.h"
#include "LPC8xx.h"

#define MAIN_RAIL_EN_PIN   0u  /* PIO0_0 -- verify against schematic */

/* PIO0_10/PIO0_11 do not exist on the LPC810's 8-pin DIP/TSSOP8 package
   (confirmed against the actual pin table: this package only brings out
   PIO0_0-PIO0_5) -- those pins are only present on the larger-package
   LPC811/LPC812 parts. PIO0_1 and PIO0_4 are the only two pins left free
   on this package after MAIN_RAIL_EN_PIN (PIO0_0), SWD (PIO0_2/PIO0_3),
   and RESET (PIO0_5) are accounted for.

   PIO0_1 is also the dedicated ISP-entry pin: the boot ROM samples it on
   every reset and enters ISP mode instead of running this firmware if it
   reads low at that moment. Because the I2C pull-ups intentionally live
   on the main MCU's switched rail (not the vault's own rail, to avoid
   parasitic drain while sleeping -- see the MCU analysis report), SDA is
   unpowered/floating during the vault's own reset, before the main rail
   has ever been turned on -- a real risk of an unwanted ISP-mode boot.
   This is a known, accepted trade-off of the 8-pin package's pin budget,
   not an oversight; verify on real hardware (Task 11) whether a pull-up
   or pull-down on this pin is needed to bias it away from the ISP
   threshold during the vault's own power-up. */
#define I2C_SDA_PIN        1u  /* PIO0_1 -- also the ISP-entry pin, see above */
#define I2C_SCL_PIN        4u  /* PIO0_4 */

void lpc810_gpio_init(void) {
    /* Enable clocks to GPIO and IOCON. Bit positions per UM10601
       Table "SYSAHBCLKCTRL register bit description" -- verify before
       flashing; this uses the commonly-documented LPC81x assignment
       (bit 6 = GPIO, bit 18 = IOCON). */
    SYSCON->SYSAHBCLKCTRL |= (1u << 6) | (1u << 18);

    /* Main rail enable pin: digital output, driven low (rail off) at boot. */
    GPIO->DIR[0] |= (1u << MAIN_RAIL_EN_PIN);
    GPIO->CLR[0] = (1u << MAIN_RAIL_EN_PIN);
}

void platform_main_rail_enable(bool on) {
    if (on) {
        GPIO->SET[0] = (1u << MAIN_RAIL_EN_PIN);
    } else {
        GPIO->CLR[0] = (1u << MAIN_RAIL_EN_PIN);
    }
}

void platform_bus_isolate(void) {
    /* IOCON PIO registers: MODE bits [4:3] = 00 selects no pull-up/down,
       and clearing the pin's function bits in the switch matrix (done in
       platform_i2c_slave_deinit(), Task 9) plus setting the pin to a
       GPIO input here leaves it as a plain high-impedance input with no
       pulls -- verify the exact IOCON bit layout against UM10601 Table
       "IOCON pin description" before flashing. */
    GPIO->DIR[0] &= ~((1u << I2C_SDA_PIN) | (1u << I2C_SCL_PIN));
    IOCON->PIO[IOCON_INDEX_PIO0_1] &= ~(0x3u << 3); /* clear MODE bits: no pull-up/down */
    IOCON->PIO[IOCON_INDEX_PIO0_4] &= ~(0x3u << 3);
}
