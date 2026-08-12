#include "vault/platform.h"
#include "em_cmu.h"
#include "em_rtc.h"

extern void efm32g210_gpio_init(void);

/* RTC->CNT / RTC->COMP0 are 24-bit registers on this Series-0 part --
   confirmed against vendor/Gecko_SDK/platform/Device/SiliconLabs/EFM32G/
   Include/efm32g_rtc.h: _RTC_CNT_MASK and _RTC_COMP0_MASK are both
   0x00FFFFFFUL, not the full 32 bits em_rtc.h's `uint32_t value`
   parameter type might suggest at a glance. RTC_CompareSet() (em_rtc.c)
   does EFM_ASSERT((value & ~0x00FFFFFF) == 0), but EFM_ASSERT compiles
   out entirely unless DEBUG_EFM is defined for the build, so in a
   release build an out-of-range compare value would not trip anything
   -- it would just get written into the 24-bit COMP0 register and
   silently truncated to its low 24 bits, producing a *much* shorter
   effective wake interval than requested (e.g. a 600 s request would
   truncate to roughly 88 s), not a build or runtime error. At the
   32.768 kHz LFXO rate this backend runs the RTC at (see
   efm32g210_clock_init() below -- no additional prescaler beyond the
   CMU_LFAPRESC0 register's DIV1 reset default is applied), (2^24-1) /
   32768 = 511 seconds is the longest interval one COMP0 match can
   represent (0x00FFFFFFu is the 24-bit mask, i.e. 2^24-1, not 2^24 --
   the division floors, so this is 511, not a round 512).
   platform_wakeup_timer_arm() clamps to this instead of letting a
   larger request silently truncate to something shorter and wrong. */
#define RTC_MAX_SECONDS (0x00FFFFFFu / 32768u) /* 511 */

static void efm32g210_clock_init(void) {
    /* HFXO (32 MHz, already populated on-board per the Olimex schematic)
       as the main/core clock. CMU_OscillatorEnable(osc, enable, wait) and
       CMU_ClockSelectSet(clock, ref) are both confirmed as real,
       non-inline functions (declared without __STATIC_INLINE) for this
       Series-0 part in the vendored em_cmu.h -- the header also defines
       an unrelated 2-arg __STATIC_INLINE stub of the same
       CMU_OscillatorEnable name, but that stub only exists under
       #if defined(_SILICON_LABS_32B_SERIES_2), which EFM32G210 (Series 0)
       does not define, so it is not the one actually used here. Both
       functions are implemented in em_cmu.c, already compiled into this
       target (see CMakeLists.txt, added in Task 3 for CMU_ClockEnable). */
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);
    CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

    /* LFXO (32.768 kHz, already populated) feeds the RTC via the LFA
       clock branch. Confirmed against em_cmu.h: cmuClock_LFA exists as
       its own branch, and cmuClock_RTC (guarded by
       #if defined(CMU_LFACLKEN0_RTC), which efm32g_rtc.h's device header
       does define for this part) is declared directly under the "LF A
       branch" section of the CMU_Clock_TypeDef enum -- i.e. RTC is fed
       from LFA specifically on this device, matching the brief's guess
       (not a differently-named branch as the brief worried it might be). */
    CMU_OscillatorEnable(cmuOsc_LFXO, true, true);
    CMU_ClockSelectSet(cmuClock_LFA, cmuSelect_LFXO);

    /* cmuClock_RTC's divisor register is CMU_LFAPRESC0 -- confirmed by
       decoding cmuClock_RTC's enum value in em_cmu.h, which encodes
       CMU_LFAPRESC0_REG as its div-register field. So this RTC *does*
       have a prescaler between LFXO/LFA and the actual counter increment
       rate, contrary to an assumption that LFXO feeds the counter
       directly. However, efm32g210f128.h confirms
       _CMU_LFAPRESC0_RESETVALUE == 0x00000000UL, i.e. DIV1 (no division)
       out of reset, which is exactly what makes
       platform_wakeup_timer_arm()'s `seconds * 32768u` arithmetic below
       correct. CMU_ClockDivSet() is called explicitly here (rather than
       leaving the prescaler at its implicit reset value) so that
       correctness is self-documenting instead of resting on an unstated
       assumption that nothing upstream ever touches LFAPRESC0. */
    CMU_ClockDivSet(cmuClock_RTC, cmuClkDiv_1);
    CMU_ClockEnable(cmuClock_RTC, true);
}

static void efm32g210_rtc_init(void) {
    RTC_Init_TypeDef rtc_init = RTC_INIT_DEFAULT;

    /* Start disabled; platform_wakeup_timer_arm() below enables the
       counter explicitly only after a real compare value has been
       programmed. Without this, RTC_INIT_DEFAULT's own `enable = true`
       would start the counter immediately at boot, counting up toward
       COMP0's post-reset value of 0 -- an immediate (harmless, since
       interrupts aren't enabled yet, but pointless) match on the very
       first tick, repeating every tick thereafter via comp0Top's
       auto-reset (see below) until the first real arm() call. */
    rtc_init.enable = false;

    /* comp0Top is left at RTC_INIT_DEFAULT's `true` -- NOT the `false`
       ("free-running counter") the brief's illustrative code guessed.
       Verified against em_rtc.h: comp0Top's doc comment is "Use compare
       register 0 as max count value", i.e. true makes the counter reset
       to 0 on every COMP0 match instead of free-running past it.
       platform_wakeup_timer_arm() must be safely callable once per wake
       cycle (the same repeated-re-arm pattern platform_lpc810_timer.c's
       WKT->COUNT write and platform_stm32u031.c's
       HAL_RTCEx_SetWakeUpTimer_IT() both already establish for the other
       two backends). With comp0Top=false the counter would keep
       free-running past each match instead of resetting, so a later
       RTC_CompareSet() call using this file's `seconds * 32768u`
       (an absolute value counted from 0) could set COMP0 below the
       counter's already-advanced current value, and it would then not
       match again until a full 24-bit counter wraparound -- effectively
       hanging the wake timer. comp0Top=true keeps each arm() call
       correct in isolation, independent of what the counter was doing
       before it, by guaranteeing it always restarts from 0 after a
       match. (rtc_init.debugRun, the third field RTC_Init_TypeDef
       actually has -- the brief's example struct only showed two field
       names -- is left at RTC_INIT_DEFAULT's `false`; no debug-halt
       behavior requirement was specified for this task.) */

    RTC_Init(&rtc_init);

    /* Enables the RTC_IRQn line in the NVIC so RTC_IRQHandler (below)
       actually runs instead of vectoring into Default_Handler's
       while(1){} -- RTC_IRQn's value (24) was independently confirmed
       against efm32g210f128.h and matches what Task 2's startup file
       vector table already uses for I2C0_IRQHandler/RTC_IRQHandler's
       array positions. */
    NVIC_EnableIRQ(RTC_IRQn);
}

void platform_init(void) {
    efm32g210_gpio_init();
    efm32g210_clock_init();
    efm32g210_rtc_init();
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    /* Clamp instead of letting a too-large request silently truncate to
       a much shorter (and wrong) interval when written into the 24-bit
       COMP0 register -- see RTC_MAX_SECONDS' comment above. Clamping
       errs toward waking sooner than requested, never later, which is
       the safe direction for a wake timer. */
    uint32_t clamped_seconds = (seconds > RTC_MAX_SECONDS) ? RTC_MAX_SECONDS : seconds;

    RTC_CompareSet(0, clamped_seconds * 32768u);
    RTC_IntEnable(RTC_IEN_COMP0);
    RTC_Enable(true);
}

void platform_wakeup_timer_clear(void) {
    RTC_IntClear(RTC_IFC_COMP0);
}

/* Real RTC interrupt handler, overriding the weak Default_Handler alias
   installed for RTC_IRQHandler in startup_efm32g210.c (same
   override-by-strong-symbol pattern platform_lpc810_timer.c's
   WKT_IRQHandler and platform_stm32u031.c's RTC_TAMP_IRQHandler both
   already use for their own wake-timer IRQ handlers). Without this, the
   vector table entry resolves to Default_Handler's silent infinite loop
   and the CPU never returns after the first RTC wake -- the same class
   of bug the vector-table off-by-one Task 2's review caught would have
   caused, but from the handler-definition side instead of the vector-
   table-layout side. Clearing the compare-match flag here is sufficient:
   there is no per-byte protocol state to drive (unlike I2C), just the
   interval-elapsed condition that needs acknowledging. */
void RTC_IRQHandler(void) {
    platform_wakeup_timer_clear();
}
