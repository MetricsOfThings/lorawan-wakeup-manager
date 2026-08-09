#ifndef PLATFORM_LPC810_UART_H
#define PLATFORM_LPC810_UART_H

/* Bring-up/debug-only UART TX (PIO0_5, 57600 8N1) -- see
   platform_lpc810_uart.c for why this pin is available and how it
   trades away the dedicated RESET function. Only built when
   LPC810_DEBUG_UART is enabled in CMake. */

void lpc810_uart_init(void);
void lpc810_uart_putc(char c);
void lpc810_uart_puts(const char *s);

#endif /* PLATFORM_LPC810_UART_H */
