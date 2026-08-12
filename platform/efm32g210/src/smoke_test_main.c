#include "em_device.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_rtc.h"
#include "em_i2c.h"
#include "em_usart.h"
#include "em_emu.h"

/* Referencing these type names (without calling anything) proves the
   vendored CMSIS + emlib headers resolve cleanly for EFM32G210F128
   under arm-none-eabi-gcc -mcpu=cortex-m3. */
static CMU_Select_TypeDef s_cmu_check;
static RTC_Init_TypeDef s_rtc_check;

int efm32g210_sdk_check_reference(void) {
    return (int)(s_cmu_check + s_rtc_check.enable);
}
