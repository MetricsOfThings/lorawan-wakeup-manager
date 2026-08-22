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
       This is independent of the RTC's own clock source, which is LSE
       (the real board's fitted 32.768 kHz crystal) -- see rtc_init()
       in platform_stm32u031.c. */
#ifdef VAULT_DIAGNOSTIC_CLOCK_HSI16
    /* Diagnostic A/B test only -- see this option's comment in
       platform/stm32u031/CMakeLists.txt. Same ~16MHz target as the
       MSI path below, different oscillator block. */
    RCC_OscInitTypeDef osc_init = {0};
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc_init.HSIState = RCC_HSI_ON;
    osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    HAL_RCC_OscConfig(&osc_init);

    RCC_ClkInitTypeDef clk_init = {0};
    clk_init.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_0);

    /* Without this, the core would resume from Stop 2 on MSI (the
       hardware default wake-up clock) even though HSI16 is the active
       clock selected above, requiring an extra reconfiguration step on
       every wake. Matches the reference firmware's own call. */
    HAL_RCCEx_WakeUpStopCLKConfig(RCC_STOP_WAKEUPCLOCK_HSI);
#else
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
#endif
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
#ifdef VAULT_DIAGNOSTIC_MINIMAL_LOOP
    /* See this option's comment in core/CMakeLists.txt. Drives the
       real, unmodified platform_stm32u031.c directly, bypassing
       vault_core_init()/vault_core_step() and vault_i2c_registers.c
       entirely -- the exact same WAKE_MAIN/BUS_ISOLATION/ARM_SLEEP
       sequence vault_core_step() runs with
       VAULT_DIAGNOSTIC_SKIP_I2C_WAIT, just called directly instead of
       through vault_core. */
    for (;;) {
        platform_i2c_slave_init(0x42);
        platform_main_rail_enable(true);
        platform_main_rail_enable(false);
        platform_i2c_slave_deinit();
        platform_bus_isolate();
        platform_wakeup_timer_arm(60);
        platform_enter_low_power_sleep();
    }
#else
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
#endif
}
