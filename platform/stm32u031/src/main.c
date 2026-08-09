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
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
