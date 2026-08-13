#include "vault/platform.h"
#include "stm32c0xx_hal.h"

/* --- SO8N pin map for STM32C011J6M6 -- provenance and status ---------
   Task 1 (see .superpowers/sdd/task-1-report.md) discovered this part's
   SO8N package is NOT wired like every other backend in this project:
   4 of its 8 physical pins are software-selectable via the SYSCFG_CFGR3
   pin-multiplexer register (HAL_SYSCFG_SetPinBinding() /
   HAL_SYSCFG_GetPinBinding(), confirmed real in the vendored
   stm32c0xx_hal.h / stm32c0xx_ll_system.h), rather than fixed in
   silicon the way e.g. the EFM32G210 or STM32U031 backends' pins are.

   Confirmed reset-default bindings (PINMUXn bits = 0 at power-on, no
   HAL_SYSCFG_SetPinBinding() call needed to have them active):
     Physical pin 1 -> PB7  (default; PC14 is the alternate)
     Physical pin 4 -> PF2  (default; PA0/PA1/PA2 are alternates)
     Physical pin 5 -> PA8  (default; PA11 is the alternate)
     Physical pin 8 -> PA14 (default; PB6/PC15 are alternates)

   This file deliberately uses ONLY the reset-default bindings above,
   so it needs zero HAL_SYSCFG_SetPinBinding() calls -- no runtime
   pin remapping means nothing to get wrong at boot on an 8-pin part
   where a wrong guess could short two signals together on real
   hardware.

   Physical pins 2/3/6/7 are NOT in the SYSCFG_CFGR3 binding table --
   Task 1 inferred (but did NOT independently confirm from a datasheet)
   that these are fixed VDD/VSS/NRST/SWDIO, consistent with an 8-pin
   power+debug+minimal-GPIO part. In particular, pin 7's identity as
   PA13/SWDIO is NOT yet confirmed against a datasheet -- Task 7 (debug
   UART, which repurposes SWDIO for UART TX) MUST verify this itself
   before relying on it; do not silently inherit this assumption.

   Concrete pin decision made here:
     MAIN_RAIL_EN = PB7  (physical pin 1, default binding). CONFIRMED
       SAFE for this task: a plain GPIO output needs no
       alternate-function capability, so this assignment does not
       depend on the still-open AF-capability question below.
     I2C SDA = PF2  (physical pin 4, default binding) -- PROVISIONAL.
       Task 5 (I2C driver) must independently confirm PF2 actually
       supports I2C1's alternate function in the vendored
       stm32c0xx_hal_gpio_ex.h before treating this as final. It is
       only used here for platform_bus_isolate()'s GPIO_MODE_ANALOG
       reconfiguration, which -- like the GPIO output above -- needs no
       AF capability either, so this pin name is safe to use for that
       purpose regardless of how the AF question resolves.
     I2C SCL = PA8  (physical pin 5, default binding) -- same
       PROVISIONAL status and same reasoning as I2C SDA above.
   ----------------------------------------------------------------- */
#define MAIN_RAIL_EN_PORT GPIOB
#define MAIN_RAIL_EN_PIN  GPIO_PIN_7
#define I2C_SDA_PORT      GPIOF
#define I2C_SDA_PIN       GPIO_PIN_2
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_8

static void gpio_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = MAIN_RAIL_EN_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MAIN_RAIL_EN_PORT, &gpio_init);
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN, GPIO_PIN_RESET);
}

void platform_main_rail_enable(bool on) {
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN,
                       on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void platform_bus_isolate(void) {
    /* Analog, no pull -- same parasitic-back-powering mitigation every
       other backend's platform_bus_isolate() already applies (design
       spec, MCU_Analysis_Report.md section 5). SDA (PF2) and SCL (PA8)
       are on different ports here (unlike other backends, since this
       part's SO8N pinout is software-selectable rather than a fixed
       table -- see the pin-map comment above), so each port needs its
       own clock enable and its own HAL_GPIO_Init() call. */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;

    gpio_init.Pin = I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);

    gpio_init.Pin = I2C_SCL_PIN;
    HAL_GPIO_Init(I2C_SCL_PORT, &gpio_init);
}

void platform_init(void) {
    HAL_Init();
    gpio_init();
    /* Task 4 (clock/RTC) and Task 7 (debug UART) append their own init
       calls here -- do not add speculative calls to functions that
       don't exist yet. */
}
