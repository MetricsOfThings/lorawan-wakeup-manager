#ifndef EFM32G210_PINS_H
#define EFM32G210_PINS_H

#include "em_gpio.h"

/* I2C0 SDA/SCL pin assignment, shared between platform_efm32g210_gpio.c
   (which puts these pins into the high-impedance bus-isolation state)
   and platform_efm32g210_i2c.c (which puts them into I2C peripheral
   function) -- both files configure the exact same two physical pins,
   in opposite directions, once per wake cycle (isolated while the main
   rail is off, driven by the I2C0 peripheral while it's on), so their
   port/pin values must be kept in lockstep. Previously each file
   locally #define'd its own copy of these four constants; consolidated
   here so there is exactly one place to update if the board's pin
   assignment ever changes.

   PD6 (SDA)/PD7 (SCL) per the Olimex EM-32G210F128-H board schematic --
   see docs/superpowers/specs/2026-08-12-efm32g210-backend-design.md §2
   and platform_efm32g210_i2c.c's I2C0_ROUTE_LOCATION comment for the
   LOCATION1 cross-check against efm32g_af_pins.h/efm32g_af_ports.h. */
#define EFM32G210_I2C_SDA_PORT gpioPortD
#define EFM32G210_I2C_SDA_PIN  6
#define EFM32G210_I2C_SCL_PORT gpioPortD
#define EFM32G210_I2C_SCL_PIN  7

#endif /* EFM32G210_PINS_H */
