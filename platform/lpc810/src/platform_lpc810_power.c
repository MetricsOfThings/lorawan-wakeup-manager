#include "vault/platform.h"
#include "LPC8xx.h"

#ifdef LPC810_DEBUG_UART
#include "platform_lpc810_uart.h"
#endif

void platform_init(void) {
    extern void lpc810_gpio_init(void);
    extern void lpc810_timer_init(void);
    lpc810_gpio_init();
    lpc810_timer_init();

#ifdef LPC810_DEBUG_UART
    lpc810_uart_init();
    lpc810_uart_puts("vault: boot\n");
#endif
}

void platform_enter_low_power_sleep(void) {
    /* PDRUNCFG selects which power domains stay alive in the sleep mode
       entered by WFI when SCR.SLEEPDEEP is set. Power-down mode (as
       opposed to Deep power-down) retains SRAM and resumes execution
       after WFI rather than resetting -- verify the exact PDRUNCFG bit
       pattern for "Power-down, SRAM retained, WKT running" against
       UM10601 "Power configuration register" before flashing; a wrong
       bit here can silently fall back to Deep power-down, which DOES
       reset on wake and would break vault_core's resume-in-place
       assumption. */
    SYSCON->PDRUNCFG = 0xFFFFFFFFu; /* placeholder pattern -- MUST be
                                        replaced with the verified
                                        Power-down bit pattern before
                                        this is flashed to hardware.
                                        (LPC8xx.h defines individual
                                        SYSCON_PDRUNCFG_*_PD field
                                        macros -- e.g. IRCOUT_PD,
                                        IRC_PD, FLASH_PD, BOD_PD,
                                        SYSOSC_PD, WDTOSC_PD, SYSPLL_PD,
                                        ACMP -- but no single "power
                                        everything down for Power-down
                                        mode" convenience macro; Task 11
                                        must OR together the correct
                                        subset per UM10601.) */

    SCB->SCR |= (1u << 2); /* SLEEPDEEP bit -- verify against ARM CMSIS core header, not UM10601 */

#ifdef LPC810_DEBUG_UART
    lpc810_uart_puts("vault: sleep\n");
#endif

    __asm volatile ("wfi");

#ifdef LPC810_DEBUG_UART
    /* If this line is ever missing from the log after a wake, that's
       direct evidence the chip reset instead of resuming in place --
       exactly the failure mode Task 11 needs to rule out. */
    lpc810_uart_puts("vault: wake\n");
#endif
}
