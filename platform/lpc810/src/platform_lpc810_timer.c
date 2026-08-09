#include "vault/platform.h"
#include "LPC8xx.h"

/* WKT counts down from a loaded value at its clock source's rate.
   Internal low-power oscillator is nominally ~10 kHz on LPC81x parts
   -- verify the exact nominal frequency and its accuracy/thermal drift
   figures against UM10601 section "Self wake-up timer (WKT)" before
   relying on this for real interval timing; this is the RTC-timing
   limitation already flagged in the design spec as a known LPC810
   bring-up limitation (no external crystal). */
#define WKT_CLOCK_HZ 10000u

void lpc810_timer_init(void) {
    /* Enable clock to WKT. Bit position per UM10601 SYSAHBCLKCTRL table
       -- verify before flashing. */
    SYSCON->SYSAHBCLKCTRL |= SYSCON_SYSAHBCLKCTRL_WKT_MASK;

    /* Select the internal low-power oscillator as the WKT clock source
       (as opposed to the external 32 kHz crystal input) -- verify the
       exact CTRL register bit/encoding against UM10601 "WKT Control
       register" before flashing. */
    WKT->CTRL = 0u;

    /* Enable the WKT interrupt line in the NVIC so WKT_IRQHandler
       (startup_lpc810.c) actually fires when the counter reaches zero. */
    NVIC_EnableIRQ(WKT_IRQn);
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    uint32_t count = seconds * WKT_CLOCK_HZ;
    /* Writing COUNT starts the countdown; WKT_IRQHandler (startup_lpc810.c)
       fires when it reaches zero. Verify the COUNT register's start-on-write
       behavior against UM10601 before relying on it. */
    WKT->COUNT = count;
}

void platform_wakeup_timer_clear(void) {
    /* Clear the WKT alarm/interrupt flag. Verify the exact flag name and
       clear mechanism (write-1-to-clear vs read-to-clear) against
       UM10601 before relying on this in an ISR. */
    WKT->CTRL |= WKT_CTRL_ALARMFLAG_MASK;
}
