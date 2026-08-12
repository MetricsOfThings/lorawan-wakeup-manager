#ifndef VAULT_PLATFORM_H
#define VAULT_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

/* Implemented once per backend (platform/lpc810, platform/stm32u031,
   platform/host_mock). core/ calls only these functions and never
   includes anything backend- or vendor-specific. */

void platform_init(void);

/* Halts the CPU core clock until any enabled interrupt fires, then
   returns immediately after (e.g. ARM Cortex-M's WFI instruction).
   Lighter than platform_enter_low_power_sleep(): peripherals, HFCLK, and
   all other clocks keep running -- only the CPU core itself idles. Used
   by vault_core's I2C wait loop so the CPU isn't spinning at full power
   while idle between I2C-interrupt-driven byte events during the
   (possibly lengthy) window the main MCU is powered and transacting. */
void platform_wait_for_interrupt(void);

/* Globally disable/enable interrupts (e.g. ARM Cortex-M's CPSID i /
   __disable_irq() and CPSIE i / __enable_irq()).

   These exist to close a lost-wakeup race in vault_core's WAKE_MAIN I2C
   wait loop. That loop is a `check condition, then WFI` pattern, and WFI
   (unlike WFE/SEV) has no event-latch: if the interrupt that would set
   the wake condition fires and is fully serviced (ISR runs to
   completion, NVIC clears "pending") in the narrow window between the
   condition being read as false and WFI actually executing, then at the
   moment WFI runs there is no pending interrupt left to wake it, and it
   blocks until some *other* interrupt happens to arrive -- which, at
   this exact point in the wake cycle, is nothing, since the wake timer
   isn't armed yet. That's a genuine, unrecoverable hang: the vault stays
   awake forever, with no path back except a physical power cycle.

   The fix is to mask interrupts around the check-and-sleep so the race
   window closes: disable interrupts, check the condition, and if still
   false, WFI, then re-enable. Critically, WFI still wakes the CPU from
   sleep even with interrupts masked (PRIMASK set) -- the ARM
   architecture guarantees this; only the ISR itself is deferred until
   interrupts are unmasked again, not the wake event. So the pending
   interrupt (masked, not lost) reliably wakes the WFI, and the next loop
   iteration observes the now-true condition. */
void platform_irq_disable(void);
void platform_irq_enable(void);

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
