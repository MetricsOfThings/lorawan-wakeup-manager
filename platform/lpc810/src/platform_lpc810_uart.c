#include <stdint.h>

#include "LPC8xx.h"

/* Bring-up/debug-only UART TX on PIO0_5 -- not part of the platform.h
   contract, not linked into production builds. Enabled only when
   LPC810_DEBUG_UART is defined (see platform/lpc810/CMakeLists.txt's
   LPC810_DEBUG_UART option).

   PIO0_5 is available here because this design gives up the dedicated
   RESET function entirely (see the pin-budget comment in
   platform_lpc810_gpio.c): nothing in this product ever resets the
   vault via an external reset line -- it only ever resumes from sleep
   via WFI/timer wake, never a hardware reset -- so trading RESET away
   for a debug UART costs nothing in normal operation. This is TX-only,
   matching the bring-up need for one-way logging: RX is intentionally
   left unassigned. */

#define UART_TX_PIN 5u /* PIO0_5, released from RESET below */

/* Assumes the default cold-boot main clock: the LPC81x's internal IRC
   running as the main clock, nominally 12 MHz, undivided
   (UARTCLKDIV = 1). This project never reconfigures the system clock
   away from that default (no external crystal, per an earlier design
   decision to keep pins free -- see the design spec). Verify the
   actual main clock frequency against UM10601 / a frequency counter
   before trusting this baud rate on real hardware; recompute
   UART_BRGVAL below if it's ever wrong. 9600 baud was chosen (over a
   faster rate like 115200) specifically because it divides much more
   cleanly from a nominal 12 MHz clock, minimizing baud error. */
#define UART_BAUD   9600u
#define UART_BRGVAL 77u /* (12000000 / (16 * 9600)) - 1 */

void lpc810_uart_init(void) {
    /* Release PIO0_5 from its fixed RESET function so it can carry a
       movable function instead. Verify the bit polarity (0 here means
       "disabled/released") against UM10601's "Pin enable register"
       description before relying on it. */
    SWM0->PINENABLE0 &= ~SWM_PINENABLE0_RESET_MASK;

    SYSCON->SYSAHBCLKCTRL |= SYSCON_SYSAHBCLKCTRL_SWM_MASK | SYSCON_SYSAHBCLKCTRL_UART0_MASK;

    /* Bring USART0 out of reset. Verify against UM10601 "Peripheral
       reset control register" whether this is actually necessary on
       this part (it may already be deasserted at cold boot); harmless
       either way. */
    SYSCON->PRESETCTRL |= SYSCON_PRESETCTRL_UART0_RST_N_MASK;

    /* Route the movable U0_TXD function onto PIO0_5. */
    SWM0->PINASSIGN0 = (SWM0->PINASSIGN0 & ~SWM_PINASSIGN0_U0_TXD_O_MASK) |
                        SWM_PINASSIGN0_U0_TXD_O(UART_TX_PIN);

    /* No division -- USART clock == main clock. Verify against
       UM10601 "USART clock divider" if a different value is needed. */
    SYSCON->UARTCLKDIV = SYSCON_UARTCLKDIV_DIV(1);

    USART0->BRG = USART_BRG_BRGVAL(UART_BRGVAL);
    USART0->CFG = USART_CFG_DATALEN(1)   /* 8 data bits */
                | USART_CFG_PARITYSEL(0) /* no parity */
                | USART_CFG_STOPLEN(0)   /* 1 stop bit */
                | USART_CFG_ENABLE_MASK;
}

void lpc810_uart_putc(char c) {
    while (!(USART0->STAT & USART_STAT_TXRDY_MASK)) {
        /* block until the transmit buffer is ready -- fine for a
           debug-only aid, never call this from a latency-sensitive
           path (e.g. the I2C0 ISR). */
    }
    USART0->TXDAT = (uint32_t)(uint8_t)c;
}

void lpc810_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            lpc810_uart_putc('\r');
        }
        lpc810_uart_putc(*s++);
    }
}
