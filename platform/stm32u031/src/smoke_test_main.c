#include "stm32u0xx_hal.h"

/* Referencing these type/macro names (without calling anything) proves
   the vendored HAL headers resolve cleanly for STM32U031xx under
   arm-none-eabi-gcc -mcpu=cortex-m0plus. */
static I2C_HandleTypeDef s_i2c_handle_check;
static RTC_HandleTypeDef s_rtc_handle_check;

int stm32u031_hal_check_reference(void) {
    return (int)(HAL_GetTick() + s_i2c_handle_check.State + s_rtc_handle_check.State);
}
