#include "vault/platform.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "efm32g210_pins.h"

/* Port/pin assignments taken from the real Olimex EM-32G210F128-H board
   user manual/schematic, not guessed -- see the design spec
   (docs/superpowers/specs/2026-08-12-efm32g210-backend-design.md) §2's
   pin table, sourced from that same document, and the README's own
   EFM32G210F128 pin table which mirrors it. Real hardware for this board
   is in hand (design spec §1); what has NOT yet happened is physical
   bring-up/verification on that hardware -- flashing this firmware and
   confirming these assignments actually work as wired, tracked as
   Task 9 (EFM32G210 hardware bring-up verification, manual).

   I2C_SDA_PORT/PIN and I2C_SCL_PORT/PIN come from efm32g210_pins.h,
   shared with platform_efm32g210_i2c.c -- see that header's comment for
   why. MAIN_RAIL_EN is only used in this file, so it stays a local
   define. */
#define MAIN_RAIL_EN_PORT gpioPortC
#define MAIN_RAIL_EN_PIN  13
#define I2C_SDA_PORT      EFM32G210_I2C_SDA_PORT
#define I2C_SDA_PIN       EFM32G210_I2C_SDA_PIN
#define I2C_SCL_PORT      EFM32G210_I2C_SCL_PORT
#define I2C_SCL_PIN       EFM32G210_I2C_SCL_PIN

/* Verified against the real vendored headers (Task 3 report):
   GPIO_PinModeSet(port, pin, mode, out), GPIO_PinOutSet(port, pin),
   GPIO_PinOutClear(port, pin) in
   vendor/Gecko_SDK/platform/emlib/inc/em_gpio.h, gpioModePushPull /
   gpioModeDisabled in the same file's GPIO_Mode_TypeDef enum, and
   CMU_ClockEnable(CMU_Clock_TypeDef clock, bool enable) in
   vendor/Gecko_SDK/platform/emlib/inc/em_cmu.h -- all match the
   brief's illustrative code exactly, no signature adjustments needed. */

void efm32g210_gpio_init(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);

    /* Main rail enable pin: digital output, driven low (rail off) at
       boot -- the `out` parameter of 0 below sets the pin's initial
       DOUT state as part of configuring it push-pull, matching the
       explicit "driven low at boot" step both other backends perform. */
    GPIO_PinModeSet(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN, gpioModePushPull, 0);
}

void platform_main_rail_enable(bool on) {
    if (on) {
        GPIO_PinOutSet(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN);
    } else {
        GPIO_PinOutClear(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN);
    }
}

void platform_bus_isolate(void) {
    /* Disabled (high-impedance, no pull), the EFM32 equivalent of the
       analog/no-pull isolation state both other backends put SDA/SCL
       into before cutting the main MCU's rail (design spec's own
       parasitic-back-powering mitigation, spec section 5 of the
       original MCU analysis report). gpioModeDisabled is emlib's
       high-impedance input mode -- verify this is truly no-pull (not a
       weak pull-up/down default) against the reference manual's GPIO
       chapter before flashing. */
    GPIO_PinModeSet(I2C_SDA_PORT, I2C_SDA_PIN, gpioModeDisabled, 0);
    GPIO_PinModeSet(I2C_SCL_PORT, I2C_SCL_PIN, gpioModeDisabled, 0);
}
