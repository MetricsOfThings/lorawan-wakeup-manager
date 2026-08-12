#include "vault/platform.h"
#include "LPC8xx.h"

#ifdef VAULT_LOG_ENABLED
#include "platform_lpc810_uart.h"
#endif

void platform_init(void) {
    extern void lpc810_gpio_init(void);
    extern void lpc810_timer_init(void);
    lpc810_gpio_init();
    lpc810_timer_init();

#ifdef VAULT_LOG_ENABLED
    lpc810_uart_init();
#endif
}

/* PDSLEEPCFG/PDAWAKECFG, not PDRUNCFG, govern power domains across a
   WFI-entered Deep-sleep/Power-down cycle on this family: PDSLEEPCFG is
   applied by hardware for the sleep period itself, and PDAWAKECFG is
   auto-loaded back into PDRUNCFG on wake -- PDRUNCFG is only the
   "currently active" register and writing to it takes effect
   immediately, not deferred to sleep entry. This used to be set up as
   `SYSCON->PDRUNCFG = 0xFFFFFFFFu` directly in
   platform_enter_low_power_sleep(), which powered down IRC_PD and
   FLASH_PD *immediately* -- while the CPU was still actively running
   from IRC-clocked flash -- freezing the chip solid before it ever
   reached the `wfi` instruction below, rather than entering a real
   WFI-based sleep that the WKT interrupt could wake it from. Both
   registers only need setting once, before the first sleep, since their
   values persist across cycles.

   BOD and the watchdog oscillator are powered down in both sleep and
   awake state (this design uses neither); the system oscillator and PLL
   are powered down too since the design runs entirely from the internal
   IRC (see platform_lpc810_uart.c's 12 MHz IRC assumption), with no
   external crystal or higher-than-IRC clock needed. IRC/IRCOUT/FLASH
   must be powered on wake (bits clear = powered) so the CPU can actually
   resume fetching and executing instructions. WKT's own low-power
   oscillator is not represented in either register at all -- by design,
   it isn't gated by PDSLEEPCFG/PDAWAKECFG the way these other domains
   are, since keeping it alive through Deep-sleep/Power-down is the
   entire point of the WKT peripheral. Verify all of this against
   UM10601's "Deep-sleep configuration register" and "Wake-up
   configuration register" sections before flashing. */
static void lpc810_power_configure_sleep_domains(void) {
    SYSCON->PDSLEEPCFG = SYSCON_PDSLEEPCFG_BOD_PD_MASK |
                          SYSCON_PDSLEEPCFG_WDTOSC_PD_MASK;

    SYSCON->PDAWAKECFG = SYSCON_PDAWAKECFG_BOD_PD_MASK |
                          SYSCON_PDAWAKECFG_SYSOSC_PD_MASK |
                          SYSCON_PDAWAKECFG_WDTOSC_PD_MASK |
                          SYSCON_PDAWAKECFG_SYSPLL_PD_MASK |
                          SYSCON_PDAWAKECFG_ACMP_MASK;
}

void platform_enter_low_power_sleep(void) {
    lpc810_power_configure_sleep_domains();

    SCB->SCR |= (1u << 2); /* SLEEPDEEP bit -- verify against ARM CMSIS core header, not UM10601 */

    __asm volatile ("wfi");
}

void platform_wait_for_interrupt(void) {
    /* SLEEPDEEP (SCB->SCR bit 2) persists once set -- platform_enter_low_power_sleep()
       above sets it and never clears it, so without explicitly clearing it
       here, any call to this function after the first full sleep cycle
       would silently fall through to Deep-sleep/Power-down instead of
       plain Sleep mode (CPU clock only, PDRUNCFG/PDSLEEPCFG domains
       untouched). Plain WFI with SLEEPDEEP clear halts only the CPU
       clock until any enabled interrupt (e.g. I2C0) fires. */
    SCB->SCR &= ~(1u << 2);
    __asm volatile ("wfi");
}

/* See vault/platform.h's doc comment for why these exist (closing a
   lost-wakeup race around vault_core's WFI-based I2C wait loop). Plain
   ARM CMSIS intrinsics -- WFI still wakes on a pending-but-masked
   interrupt, per the ARM architecture. */
void platform_irq_disable(void) {
    __disable_irq();
}

void platform_irq_enable(void) {
    __enable_irq();
}
