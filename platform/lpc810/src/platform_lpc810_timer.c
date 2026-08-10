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

    /* NVIC_EnableIRQ above only makes WKT_IRQHandler run *after* the CPU
       has already woken up -- it does not, by itself, let WKT wake the
       CPU out of Deep-sleep/Power-down in the first place. This part's
       "start logic" (STARTERP0/STARTERP1) is a separate wake-source
       enable layer sitting in front of the NVIC specifically for these
       deeper sleep modes; STARTERP1 bit 15 (WKT) is what actually
       arms WKT as a wake source. Without this, platform_enter_low_power_sleep()
       (fixed to stop corrupting PDRUNCFG at write time, see that file's
       history) genuinely reaches WFI and enters Power-down, but the WKT
       alarm firing never pulls the CPU back out of it -- it can only
       wake later from some other event (e.g. a debugger reset), which
       looks identical to "never wakes" from the outside. Verify the
       STARTERP1 bit position and write semantics (this assumes a plain
       read-modify-write enable, not write-1-to-set-only) against
       UM10601's "Start logic edge control register" / "Start logic 0
       wake-up enable register 1" sections before flashing. */
    SYSCON->STARTERP1 |= SYSCON_STARTERP1_WKT_MASK;
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

/* Real WKT interrupt handler, overriding the weak Default_Handler alias
   installed for WKT_IRQHandler in startup_lpc810.c (same override-by-
   strong-symbol pattern I2C0_IRQHandler already uses in
   platform_lpc810_i2c.c, from Task 9). Without this, the vector table
   entry resolves to Default_Handler's while(1){} and the CPU never
   returns to vault_core_step() after the first sleep. Clearing the
   alarm flag here is sufficient: there is no per-byte protocol state to
   drive (unlike I2C), just the countdown-elapsed condition that needs
   acknowledging so WKT can be rearmed next cycle. */
void WKT_IRQHandler(void) {
    platform_wakeup_timer_clear();
}
