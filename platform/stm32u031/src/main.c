#include "vault/vault_core.h"
#include "vault/platform.h"
#include "stm32u0xx_hal.h"

void SystemClock_Config(void) {
    /* Raised from the original placeholder ~4 MHz (RCC_MSIRANGE_6) to
       ~16 MHz (RCC_MSIRANGE_8) -- real hardware testing traced the I2C2
       COMMAND-register NACK saga (see platform_stm32u031.c's
       platform_i2c_slave_init() comment) partly to how little real CPU
       time was available, at 4 MHz, for the byte-by-byte ISR-driven
       receive path (HAL_I2C_SlaveRxCpltCallback -> register-map logic
       -> re-arm the next byte) to keep up with the bus between clock
       edges. This also raises PCLK1 (AHB/APB1 dividers are both DIV1,
       so HCLK=SYSCLK=PCLK1 always move together), which is I2C2's own
       kernel clock -- see the re-derived Timing value in
       platform_i2c_slave_init() for the corresponding TIMINGR update.
       This is independent of the RTC's own clock source (LSI by
       default -- LSE has not oscillated on any board revision tried
       so far; see VAULT_DIAGNOSTIC_RTC_LSE and rtc_init()'s own
       comments in platform_stm32u031.c). */
    RCC_OscInitTypeDef osc_init = {0};
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc_init.MSIState = RCC_MSI_ON;
    osc_init.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc_init.MSIClockRange = RCC_MSIRANGE_8; /* ~16 MHz */
    HAL_RCC_OscConfig(&osc_init);

    RCC_ClkInitTypeDef clk_init = {0};
    clk_init.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    /* FLASH_LATENCY_1 (one wait state), not _0: this datasheet defers
       the exact HCLK-frequency-vs-wait-state table to RM0503 (the
       reference manual, not available locally) -- rather than assume
       0 wait states is still valid at 16 MHz (it was only ever
       verified correct for the original ~4 MHz), pay the small,
       low-risk cost of one extra wait state. Verify against RM0503's
       "Number of wait states according to CPU clock (HCLK) frequency"
       table and drop back to _0 only if it's confirmed unnecessary. */
    HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_1);
}

int main(void) {
    /* platform_enter_low_power_sleep() calls SystemClock_Config() on
       resume from Stop 2 (the clock tree needs reconfiguring after
       exiting stop mode), but that left the very first boot without any
       clock tree configuration at all -- HAL_Init() alone doesn't set up
       the MSI/SYSCLK tree SystemClock_Config() defines. Call it here so
       the first pass through platform_init()/vault_core_init() runs
       with the intended clock configuration too. */
    SystemClock_Config();
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
