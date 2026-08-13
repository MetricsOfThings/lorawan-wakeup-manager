#include <string.h>

#include "stm32c0xx_hal.h"
#include "vault/vault_log.h"

/* Bring-up/debug-only UART TX, VAULT_LOG_ENABLED-gated (see
   platform/stm32c011/CMakeLists.txt) -- not part of the platform.h
   contract.

   *** CORRECTED against the real STM32C011x4/x6 datasheet (DS13866) ***
   The design spec (section 7) and this task's original brief assumed
   this pin would be SWDIO (PA13). That is wrong: PA13's alternate-
   function list per DS13866 is SWDIO, IR_OUT, TIM3_ETR, USART2_RX,
   EVENTOUT -- USART TX is not reachable from PA13 at all, only RX.

   The pin actually repurposed here is SWCLK (PA14, physical pin 8,
   which per the pin-map comment in platform_stm32c011.c is this SO8N
   part's SYSCFG_CFGR3 reset-default binding, no HAL_SYSCFG_SetPinBinding()
   needed). PA14's real AF table per DS13866 is SWCLK (AF0, reset
   default), USART2_TX (AF1), EVENTOUT, SPI1_NSS/I2S1_WS, USART2_RX,
   TIM1_CH1, MCO2, USART1_RTS_DE_CK -- USART2_TX is confirmed present
   as AF1.

   Confirmed against the vendored stm32c0xx_hal_gpio_ex.h, not just the
   datasheet: GPIO_AF1_USART2 (0x01) is defined unconditionally for all
   parts in this family, including STM32C011xx, while GPIO_AF0_USART2
   is compiled out entirely for STM32C011xx (only defined under
   `#if defined(STM32C031xx) || ... `) -- i.e. AF0 on this part is
   SWCLK/SWJ, never USART2, and AF1 is the only USART2 mapping this
   part's HAL exposes at all. This matches the datasheet's AF1 =
   USART2_TX / AF0 = SWCLK claim exactly.

   Practical consequence, same as the design spec's original (mistaken-
   pin) analysis: SWD requires both SWDIO and SWCLK, so repurposing
   SWCLK alone already breaks live SWD debugging for the duration of a
   VAULT_LOG_ENABLED build, just as repurposing SWDIO would have. SWDIO
   (PA13) itself is untouched by this file.

   TX-only, matching the pattern every other backend's UART file in
   this project already uses (bring-up logging need is one-way) -- RX
   (USART2_RX, also reachable from PA14 via a different AF, or from
   PA13) is intentionally left unconfigured. */

static UART_HandleTypeDef s_uart_handle;

void stm32c011_uart_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_14;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Alternate = GPIO_AF1_USART2; /* confirmed against both
                                               DS13866's AF table for
                                               PA14 and the vendored
                                               stm32c0xx_hal_gpio_ex.h --
                                               see the file comment
                                               above. */
    HAL_GPIO_Init(GPIOA, &gpio_init);

    __HAL_RCC_USART2_CLK_ENABLE();

    s_uart_handle.Instance = USART2;
    s_uart_handle.Init.BaudRate = 57600; /* matches the fixed baud rate
                                             every other backend already
                                             settled on */
    s_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart_handle.Init.StopBits = UART_STOPBITS_1;
    s_uart_handle.Init.Parity = UART_PARITY_NONE;
    s_uart_handle.Init.Mode = UART_MODE_TX;
    s_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&s_uart_handle);
}

/* core/'s vault_log() contract (vault/vault_log.h). Only called from
   vault_core's normal thread-mode control flow, never from the I2C1
   ISR -- see the comment in vault_core.c. */
void vault_log(const char *msg) {
    /* HAL_UART_Transmit()'s blocking mode does genuinely wait for
       transmission complete, not merely buffer-empty -- verified by
       reading its real implementation in the vendored
       stm32c0xx_hal_uart.c (HAL_UART_Transmit(), Src/stm32c0xx_hal_uart.c)
       rather than assumed by analogy. Its per-byte loop waits on
       UART_FLAG_TXE (buffer empty) before writing each byte to TDR, but
       after the loop finishes it additionally calls
       UART_WaitOnFlagUntilTimeout(huart, UART_FLAG_TC, ...) -- TC being
       "Transmission Complete", i.e. the shift register has actually
       finished pushing the last bit onto the wire -- before returning
       HAL_OK. That closes exactly the gap this project's EFM32G210
       backend had to fix by hand (its emlib USART_Tx() only waits for
       TXBL/buffer-empty, requiring an explicit extra spin on
       STATUS_TXC after the write loop to avoid truncating the last
       byte of a message right before EM2/Stop-mode entry cuts the
       peripheral clock). No equivalent extra wait is needed here: this
       call already blocks until TC on its own. */
    HAL_UART_Transmit(&s_uart_handle, (const uint8_t *)msg,
                       (uint16_t)strlen(msg), HAL_MAX_DELAY);
}
