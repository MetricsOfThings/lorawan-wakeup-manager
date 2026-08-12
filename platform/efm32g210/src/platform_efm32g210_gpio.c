#include "vault/platform.h"
#include "em_cmu.h"
#include "em_gpio.h"

/* Port/pin assignments per the brief; not yet cross-checked against a
   real schematic (none was available to this task) -- verify against
   the actual EFM32G210F128 board schematic before flashing, same
   caveat platform_lpc810_gpio.c's MAIN_RAIL_EN_PIN comment and
   platform_stm32u031.c's pin choices carry. */
#define MAIN_RAIL_EN_PORT gpioPortC
#define MAIN_RAIL_EN_PIN  13
#define I2C_SDA_PORT      gpioPortD
#define I2C_SDA_PIN       6
#define I2C_SCL_PORT      gpioPortD
#define I2C_SCL_PIN       7

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
