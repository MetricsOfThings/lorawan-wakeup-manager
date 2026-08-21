#include "stm32u0xx_hal.h"
#include "vault/vault_log.h"

/* Bring-up/debug-only UART TX on PA2 (USART2, AF7) -- not part of the
   platform.h contract, only built when VAULT_LOG_ENABLED is on (see
   platform/stm32u031/CMakeLists.txt). Unlike the LPC810 backend, this
   part's 16 usable I/Os mean no pin trade-off is needed: PA0 (main
   rail), PA9/PA10 (I2C1) are already committed, and PA2 is free.
   TX-only, matching the bring-up need for one-way logging -- RX
   (PA3) is intentionally left unconfigured. */

static UART_HandleTypeDef s_uart_handle;

void stm32u031_uart_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_2;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Alternate = GPIO_AF7_USART2; /* confirmed against the
                                               STM32U031 datasheet's
                                               alternate-function table
                                               for PA2 */
    HAL_GPIO_Init(GPIOA, &gpio_init);

    s_uart_handle.Instance = USART2;
    s_uart_handle.Init.BaudRate = 57600;
    s_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart_handle.Init.StopBits = UART_STOPBITS_1;
    s_uart_handle.Init.Parity = UART_PARITY_NONE;
    s_uart_handle.Init.Mode = UART_MODE_TX;
    s_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    /* HAL_UART_Init() computes the baud-rate divisor from the current
       APB clock (see SystemClock_Config() in main.c). If that clock
       ever changes, this baud rate is still requested correctly --
       HAL recomputes the divisor, unlike the LPC810 backend's manual
       BRGVAL, which would need updating by hand. */
    HAL_UART_Init(&s_uart_handle);
}

/* Unlike I2C_SDA/SCL (platform_bus_isolate() in platform_stm32u031.c),
   PA2/USART2_TX was never put into a defined low-power state before
   Stop 2 entry -- real hardware showed ~900uA Stop-mode current
   (expected ~600nA) specifically on VAULT_LOG_ENABLED builds. A GPIO
   pin left configured as Alternate Function for a peripheral whose
   clock Stop 2 gates (PCLK1, feeding USART2) can end up electrically
   floating rather than driven, which pulls real current through the
   input buffer's partially-conducting transistors near the CMOS
   switching threshold ("shoot-through") -- the same class of leakage
   the I2C isolation step already exists to prevent. Called from
   platform_stm32u031.c's platform_enter_low_power_sleep(), which also
   restores the pin on wake since USART2 itself is initialized once at
   boot and expected to keep working across sleep cycles (unlike I2C2,
   which is fully deinit/reinit per cycle). */
void stm32u031_uart_pin_isolate(void) {
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_2;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}

void stm32u031_uart_pin_restore(void) {
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_2;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}

/* core/'s vault_log() contract (vault/vault_log.h). Only called from
   vault_core's normal thread-mode control flow, never from the I2C1
   ISR -- see the comment in vault_core.c. */
void vault_log(const char *msg) {
    uint16_t len = 0;
    while (msg[len] != '\0') {
        len++;
    }
    /* Blocking transmit with a generous timeout -- this is a
       debug-only aid, not a latency-sensitive path. */
    HAL_UART_Transmit(&s_uart_handle, (const uint8_t *)msg, len, 100);
}
