#include "vault/vault_core.h"
#include "vault/platform.h"
#include "stm32u0xx_hal.h"

void SystemClock_Config(void) {
    /* Placeholder MSI-based configuration -- revisit once real hardware
       exists to pick the actual clock tree (e.g. whether LSE is fitted
       for RTC accuracy, matching the STM32U031F8P6's 16-pin budget
       advantage over the LPC810 called out in the analysis report). */
    RCC_OscInitTypeDef osc_init = {0};
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc_init.MSIState = RCC_MSI_ON;
    osc_init.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc_init.MSIClockRange = RCC_MSIRANGE_6; /* ~4 MHz -- verify against
                                                 the actual clock budget
                                                 once hardware exists */
    HAL_RCC_OscConfig(&osc_init);

    RCC_ClkInitTypeDef clk_init = {0};
    clk_init.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_0);
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
