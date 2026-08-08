#include "vault/platform.h"
#include "LPC8xx.h"

#define MAIN_RAIL_EN_PIN   0u  /* PIO0_0 -- verify against schematic */
#define I2C_SDA_PIN       10u  /* PIO0_10 -- verify against schematic */
#define I2C_SCL_PIN       11u  /* PIO0_11 -- verify against schematic */

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
    IOCON->PIO[IOCON_INDEX_PIO0_10] &= ~(0x3u << 3); /* clear MODE bits: no pull-up/down */
    IOCON->PIO[IOCON_INDEX_PIO0_11] &= ~(0x3u << 3);
}
