#ifndef VAULT_PLATFORM_H
#define VAULT_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

/* Implemented once per backend (platform/lpc810, platform/stm32u031,
   platform/host_mock). core/ calls only these functions and never
   includes anything backend- or vendor-specific. */

void platform_init(void);

/* Arms the wakeup timer/RTC to fire in `seconds` seconds. */
void platform_wakeup_timer_arm(uint32_t seconds);
void platform_wakeup_timer_clear(void);

/* Turns the main Application Processor's power rail on/off. */
void platform_main_rail_enable(bool on);

/* Starts/stops the I2C peripheral in slave mode at 7-bit address `addr`.
   While active, incoming bytes and read requests must be routed to
   vault_i2c_registers_on_write_byte() / on_read_request() / on_stop()
   (declared in vault/vault_i2c_registers.h), typically from the
   backend's I2C interrupt handler. */
void platform_i2c_slave_init(uint8_t addr);
void platform_i2c_slave_deinit(void);

/* Puts the I2C SDA/SCL pins into a high-impedance state (analog input,
   no pull-up/down) so a powered-down main MCU cannot back-power this
   MCU through its GPIO ESD diodes. Must be called after
   platform_i2c_slave_deinit() and before platform_main_rail_enable(false). */
void platform_bus_isolate(void);

/* Enters the deepest low-power mode that still retains SRAM and CPU
   register state, and blocks until the armed wakeup timer fires.
   Must return with execution continuing on the line after the call
   (no reset), consistent with STM32 Stop 2 / LPC81x Power-down mode. */
void platform_enter_low_power_sleep(void);

#endif /* VAULT_PLATFORM_H */
