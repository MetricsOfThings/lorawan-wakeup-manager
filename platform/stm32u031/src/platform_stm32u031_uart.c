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

/* Bring-up diagnostic: bypasses HAL_UART_Transmit() entirely, writing
   directly to USART2->TDR with an unbounded per-byte poll on ISR.TXE
   (no HAL, no HAL_GetTick()/timeout dependency at all) -- isolates
   whether HAL_UART_Transmit()'s own logic (its tick-based timeout in
   particular) is truncating transmission, versus the USART2 peripheral
   or its clock genuinely being unable to send past a certain point. If
   this also truncates identically, the problem is below HAL entirely;
   if it sends cleanly, the problem is specifically in
   HAL_UART_Transmit(). Not part of the vault_log() contract -- called
   directly from platform_init() for this one diagnostic pass only. */
void stm32u031_uart_raw_transmit(const char *msg) {
    uint16_t len = 0;
    while (msg[len] != '\0') {
        len++;
    }
    for (uint16_t i = 0; i < len; i++) {
        while ((USART2->ISR & USART_ISR_TXE_TXFNF_Msk) == 0) {
            /* Unbounded wait -- if the peripheral is genuinely stuck,
               this hangs here forever rather than silently truncating,
               which is itself useful diagnostic information (no further
               bytes AND no further log lines ever appear). */
        }
        USART2->TDR = (uint32_t)(uint8_t)msg[i];
    }
    while ((USART2->ISR & USART_ISR_TC) == 0) {
        /* Wait for the last byte to fully leave the shift register. */
    }
}
