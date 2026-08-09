#include <stdint.h>

#include "LPC8xx.h"
#include "vault/vault_log.h"

/* Bring-up/debug-only UART TX on PIO0_5 -- not part of the platform.h
   contract, not linked into production builds. Enabled only when
   VAULT_LOG_ENABLED is defined (see core/CMakeLists.txt's
   VAULT_LOG_ENABLED option, which platform/lpc810/CMakeLists.txt reads
   to decide whether to build this file at all).

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
   UART_BRGVAL below if it's ever wrong.

   57600 was chosen over 115200 because it divides far more cleanly
   from a nominal 12 MHz clock: BRGVAL=12 gives an actual rate of
   57692 baud (0.16% error) versus 115200's best case of ~7% error at
   this clock, which is above the usual ~2-3% margin considered safe
   for reliable UART framing. */
#define UART_BAUD   57600u
#define UART_BRGVAL 12u /* (12000000 / (16 * 57600)) - 1, rounded; actual rate 57692 baud, 0.16% error */

void lpc810_uart_init(void) {
    /* SWM's own clock must be enabled before its registers can be
       reliably written -- matching the working pattern already used in
       platform_lpc810_i2c.c's lpc810_i2c_pins_to_i2c_function(), which
       enables SYSCON_SYSAHBCLKCTRL_SWM_MASK before touching any SWM
       register. This must come before the PINENABLE0 write below, not
       after: writing to SWM while unclocked was silently ignored,
       leaving PIO0_5 stuck in its default RESET function and never
       actually routed to U0_TXD. */
    SYSCON->SYSAHBCLKCTRL |= SYSCON_SYSAHBCLKCTRL_SWM_MASK | SYSCON_SYSAHBCLKCTRL_UART0_MASK;

    /* Release PIO0_5 from its fixed RESET function so it can carry a
       movable function instead. Confirmed on real hardware (Task 11)
       that this bit is active-LOW, opposite of what the register's own
       name ("Pin enable register") suggests: while connected live via
       SWD, PINENABLE0 read back with the SWCLK/SWDIO/RESET bits all at
       0 -- the only sensible reading, since those fixed functions were
       demonstrably still active (SWD was working; RESET's fixed
       function was still claiming PIO0_5, which is why U0_TXD's
       correctly-configured SWM routing never reached the physical pin).
       So 0 = fixed function ACTIVE, 1 = released to a movable function
       -- the previous `&= ~MASK` (clearing the bit) was keeping RESET
       active, not releasing it. */
    SWM0->PINENABLE0 |= SWM_PINENABLE0_RESET_MASK;

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

/* core/'s vault_log() contract (vault/vault_log.h). Only called from
   vault_core's normal thread-mode control flow, never from the I2C0
   ISR -- see the comment in vault_core.c. */
void vault_log(const char *msg) {
    lpc810_uart_puts(msg);
}
