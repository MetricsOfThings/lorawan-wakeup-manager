#include "vault/platform.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
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
   effective wake interval than requested, not a build or runtime error.

   Rather than run the RTC at the raw 32.768 kHz LFXO rate (which would
   cap represesentable intervals at (2^24-1) / 32768 ~= 511 seconds --
   far short of VAULT_REG_WAKE_INTERVAL_SEC's full 4-byte range, and much
   less than the other two backends' effective ceiling), efm32g210_clock_init()
   below sets the LFAPRESC0 divisor to cmuClkDiv_32 (DIV32), turning the
   RTC into a 1024 Hz counter: COMP0 == seconds * 1024, and the ceiling
   becomes (2^24-1) / 1024 ~= 16383 seconds (~4.55 hours) -- far past any
   wake interval this protocol's design docs describe (minutes to low
   hours). Confirmed cmuClkDiv_32 is a real, valid CMU_ClockDivSet()
   divisor for the RTC's LFAPRESC0 register against the vendored
   em_cmu.h (_CMU_LFAPRESC0_RTC_DIV32 == 0x5, confirmed in
   efm32g210f128.h).

   An earlier version of this fix used cmuClkDiv_32768 (a genuine 1 Hz
   counter, COMP0 == seconds directly, ~194-day ceiling). That was
   reverted: RTC_Enable() and RTC_CompareSet() (vendored em_rtc.c) both
   call an internal regSync() that busy-waits on RTC->SYNCBUSY for the
   write to propagate into the RTC's own low-frequency clock domain --
   and on this Gecko-family part (_EFM32_GECKO_FAMILY, confirmed in
   efm32g210f128.h), that domain runs at the RTC's own *post-prescaler*
   clock, because CMU_LFAPRESC0 divides the clock in the CMU before it
   ever reaches the RTC block (see efm32g210_clock_init()'s comment) --
   the RTC receives only the already-divided clock, and both its counter
   and its sync handshake logic run on that one clock. At 1 Hz, each
   regSync() call (documented as resolving in about 3 clock cycles) costs
   roughly 3 seconds of real busy-wait, and platform_wakeup_timer_arm()
   below calls RTC_Enable()/RTC_CompareSet()/RTC_Enable() -- five
   regSync() calls total -- every single time it arms the wake timer,
   which is every wake cycle. That is a full-power busy-wait of up to
   ~15 seconds added to every arm() call, silently eating into the
   requested interval and burning exactly the power this design exists
   to avoid. At 1024 Hz (cmuClkDiv_32), the same ~3-cycle sync cost is
   roughly 3ms per call (~15ms worst case for all five calls per arm()) --
   negligible, not power-relevant, and the ceiling still comfortably
   covers this protocol's realistic wake intervals.

   The clamp below still exists as a defensive floor/ceiling on the
   24-bit register itself, in case something upstream ever changes the
   prescaler again. */
#define RTC_MAX_SECONDS 16383u /* (2^24-1) / 1024, ~4.55 hours at 1024 Hz */

/* RTC tick rate after efm32g210_clock_init()'s cmuClkDiv_32 prescaler
   (32768 Hz LFXO / 32 == 1024 Hz). platform_wakeup_timer_arm() multiplies
   requested seconds by this to get the COMP0 tick count. */
#define RTC_TICKS_PER_SEC 1024u

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
       CMU_LFAPRESC0_REG as its div-register field. This RTC has a real
       prescaler between LFXO/LFA and the actual counter increment rate,
       and CMU_LFAPRESC0_RTC's field is 4 bits wide with encodings for
       DIV1 through DIV32768 (confirmed against efm32g210f128.h:
       _CMU_LFAPRESC0_RTC_MASK == 0xF, _CMU_LFAPRESC0_RTC_DIV32 == 0x5).
       This prescaler divide happens in the CMU, upstream of the RTC
       block itself -- the RTC's own clock input is already the divided
       result. Using cmuClkDiv_32 here divides the 32.768 kHz LFXO down
       to a 1024 Hz RTC counter rate (see RTC_MAX_SECONDS' comment above
       for why DIV32 was chosen over the more aggressive DIV32768, and
       RTC_TICKS_PER_SEC for the resulting tick-to-seconds arithmetic)
       -- this replaces an earlier DIV1 (no division) configuration that
       left COMP0's 24-bit width as a hard ~511-second wake-interval
       ceiling, silently far short of VAULT_REG_WAKE_INTERVAL_SEC's full
       4-byte range. */
    CMU_ClockDivSet(cmuClock_RTC, cmuClkDiv_32);

    /* RTC is a Low Energy (LE) peripheral: on this Series-0 part, the
       CPU-side register interface for LE peripherals (RTC, LETIMER,
       PCNT, LEUART) is gated by a separate bridge clock,
       CMU_HFCORECLKEN0_LE ("Low Energy Peripheral Interface Clock
       Enable" -- confirmed in efm32g210f128.h, RESETVALUE 0 i.e.
       disabled by default), distinct from cmuClock_RTC above (which
       only clocks the RTC's own LFA-derived counter, not the bus
       bridge the CPU uses to read/write its registers). Without this,
       RTC_Init()/RTC_CompareSet()/RTC_IntEnable() writes go through an
       unclocked bridge -- they don't fault, they just don't reliably
       take effect, so COMP0 never actually matches and the wake
       interrupt never fires: the device sleeps and never wakes. Must
       be enabled before efm32g210_rtc_init() touches any RTC register. */
    CMU_ClockEnable(cmuClock_HFLE, true);
    CMU_ClockEnable(cmuClock_RTC, true);

    /* Silicon Labs EFM32G errata I2C_E102 ("I2C Disabled After EM2/EM3",
       silabs.com/documents/public/errata/efm32g-errata.pdf): if USART0's
       clock is disabled, the I2C module comes back up disabled after
       waking from EM2/EM3, and stays disabled until USART0's clock is
       enabled -- even though this backend never uses USART0 itself
       (I2C0 is on its own peripheral; USART1 is the debug UART). The
       documented workaround is simply to keep USART0's clock enabled
       whenever I2C is used. This erratum exists on chip revisions A and
       B (fixed in C); this part's exact silicon revision was not
       confirmed against the datasheet's revision-marking scheme, so the
       workaround is applied unconditionally -- it is a documented no-op
       on unaffected revisions (USART0 is otherwise idle: HFPERCLK-derived
       peripheral clocks are gated off during EM2 regardless of this
       enable bit, so this does not add any EM2 sleep current). Enabled
       once here at boot, not tied to platform_i2c_slave_init()/_deinit()'s
       per-cycle lifecycle: the erratum is specifically about the state
       crossing an EM2/EM3 sleep, so toggling it off before sleep (to
       mirror I2C0's own teardown) would defeat the workaround. */
    CMU_ClockEnable(cmuClock_USART0, true);
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
       RTC_CompareSet() call using this file's `seconds * RTC_TICKS_PER_SEC`
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
    /* Silicon Labs' own CHIP_Init() (vendored em_chip.h) -- its doc
       comment states plainly "This function must be called immediately
       in main()" for revision errata workarounds, and this backend was
       never calling it. For _EFM32_GECKO_FAMILY (this part), it bundles
       several silicon-revision-gated fixes, most notably: forcing a set
       of CMU registers (0x400C8040/44/58/60/78) to their documented
       reset values on early silicon (otherwise left in an unspecified,
       manufacturing-variable state out of POR -- a very plausible source
       of behavior that varies device-to-device or is intermittent
       between wake cycles); an EM2/3-related DMA clock enable for rev A;
       and -- confirmed by reading its source -- the *exact* rev-A/B
       "USART0 clock must be enabled after waking from EM2/EM3 to get I2C
       to work" fix efm32g210_clock_init()'s own cmuClock_USART0 enable
       below already applies by hand (matching Silicon Labs' published
       I2C_E102 errata). That hand-applied fix stays in place as a
       revision-independent belt-and-braces measure, but CHIP_Init() is
       the complete, officially-mandated set and must run first, before
       any other peripheral init touches these registers. */
    CHIP_Init();

    /* Silicon Labs EFM32G errata EMU_E101 ("EM Transition Brown Out") and
       CMU_E104 ("Energy Mode Transitions Cause HFRCO Overshoot"), both
       from silabs.com/documents/public/errata/efm32g-errata.pdf, exist on
       chip revisions A and B and are fixed in hardware from revision C:
       transitioning between energy modes (e.g. EM2 wake, exactly this
       backend's wake path) can spuriously brown-out reset the device, or
       cause the HFRCO to overshoot its configured frequency by up to 50%
       (i.e. above the part's 32 MHz limit) before settling, which can
       itself trigger a BOD reset. Both are plausible, silicon-documented
       causes of a spontaneous reset landing exactly at an EM2 wake
       boundary -- which is what real hardware testing showed: debug UART
       output correctly reached "vault_core: sleep" but the *next* line
       observed was not "vault_core: wake" as expected, it was
       "vault_core: init" (vault_core_init()'s own one-time boot message)
       appearing again mid-capture, meaning the device actually reset
       during/after the EM2 transition rather than resuming in place --
       and cycles that didn't fully reset still showed garbled bytes
       exactly at that same boundary, consistent with the core briefly
       running at an unstable/overshot clock frequency (which would also
       explain the paired, harder-to-pin-down intermittent I2C0 data
       corruption reported during hardware bring-up: I2C0's bit-sampling
       clock is derived from the same HFCLK).

       Neither fix is included in CHIP_Init() above (confirmed by reading
       its vendored source in full) -- unlike the fixes CHIP_Init() does
       apply unconditionally, both of these carry an explicit "not
       compatible with devices of later revisions where this erratum has
       been corrected" warning in the errata text, so applying them
       blindly on already-fixed silicon risks writing to a register field
       that means something different there. They are therefore
       explicitly gated on the same chipRev.major==1 (rev A/B use minor
       0/1) check CHIP_Init() itself uses for its own rev-A/B-only fixes,
       rather than applied unconditionally. */
    {
        SYSTEM_ChipRevision_TypeDef chip_rev;
        SYSTEM_ChipRevisionGet(&chip_rev);
        if (chip_rev.major == 1 && chip_rev.minor <= 1) {
            /* EMU_E101: prevents spurious brown-out resets on EM
               transitions. Costs ~4% extra current in EM0/EM1 per the
               errata -- accepted here since a spontaneous mid-cycle
               reset is far worse for this design than a small EM0/EM1
               current increase (EM2 sleep current, where this device
               spends nearly all its time, is unaffected). */
            *(volatile uint32_t *)0x400C6020 |= 0x6000u;

            /* CMU_E104: prevents HFRCO frequency overshoot (up to 50%,
               i.e. potentially over the part's 32 MHz limit) during
               energy mode transitions, which the errata states can
               itself trigger a BOD reset. Costs ~10 uA extra current in
               EM0/EM1 per the errata -- same tradeoff rationale as
               EMU_E101 above. Not compatible with the ADC_E108 fix (this
               backend does not use the ADC, so no conflict). */
            *(volatile uint32_t *)0x400C6018 = 0xC201u;
        }
    }

    efm32g210_gpio_init();
    efm32g210_clock_init();
    efm32g210_rtc_init();

#ifdef VAULT_LOG_ENABLED
    /* efm32g210_uart_init() (platform_efm32g210_uart.c, only compiled
       when VAULT_LOG_ENABLED -- see CMakeLists.txt) must run after
       efm32g210_clock_init() above, not before: its USART1 baud-rate
       calculation reads the live HFCLK source (see the detailed
       comment in platform_efm32g210_uart.c), which is only correct
       once efm32g210_clock_init() has actually switched HF to HFXO. */
    extern void efm32g210_uart_init(void);
    efm32g210_uart_init();
#endif
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    /* Clamp instead of letting a too-large request silently truncate to
       a much shorter (and wrong) interval when written into the 24-bit
       COMP0 register -- see RTC_MAX_SECONDS' comment above. Clamping
       errs toward waking sooner than requested, never later, which is
       the safe direction for a wake timer. With the RTC now running at
       1024 Hz (efm32g210_clock_init()'s cmuClkDiv_32 prescaler), COMP0
       == seconds * RTC_TICKS_PER_SEC -- clamp first (in seconds) so the
       multiply below can't overflow or wrap past the 24-bit register. */
    uint32_t clamped_seconds = (seconds > RTC_MAX_SECONDS) ? RTC_MAX_SECONDS : seconds;
    uint32_t comp0_ticks = clamped_seconds * RTC_TICKS_PER_SEC;

    /* RTC_Enable(true) on an already-enabled RTC is a no-op for the
       counter -- it only writes CTRL.EN (see em_rtc.c's RTC_Enable(),
       BUS_RegBitWrite), it does not reset CNT. Without resetting the
       counter here, CNT free-runs through the entire previous WAKE_MAIN
       window (the I2C exchange with the main MCU) before this function
       reprograms COMP0, so the actual sleep duration would silently be
       `seconds - T_awake` instead of `seconds` from every second cycle
       onward -- diverging from both other backends, whose wake timers
       restart cleanly at arm time. Disabling first, then setting the new
       compare value, then re-enabling guarantees CNT restarts from 0
       with the new COMP0 already in place -- avoiding the alternative
       hazard of setting COMP0 while CNT is still running from the old
       cycle and already past the new value (which would require a full
       24-bit wraparound to match again). */
    RTC_Enable(false);
    RTC_CompareSet(0, comp0_ticks);
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

/* EM2 (Deep Sleep) -- retains RAM and CPU state, keeps the RTC/LFXO
   running (LFXO feeds the RTC via the LFA branch, and EM2 only disables
   the *high*-frequency clocks, per em_emu.c's EMU_EnterEM2()/EMU_EnterEM3()
   doc comments -- EM3 is the mode that additionally disables LFXO/LFRCO
   by software), matching vault_core's resume-in-place assumption. EM3 is
   not used here for exactly that reason: it would stop the RTC and this
   backend's timed wake (platform_wakeup_timer_arm()) would never fire.
   `true` (the `restore` argument) asks EMU_EnterEM2() to save/restore
   oscillator and clock state across the sleep, which is the documented,
   non-EM01VSCALE-specific EFM32G behavior on this Series-0 part (the
   EM01 voltage-scaling restore path in em_emu.c is gated on
   EMU_VSCALE_EM01_PRESENT/_SILICON_LABS_32B_SERIES >= 2, neither of which
   this part defines) -- it does not affect GPIO output latching, which on
   this part is controlled by the GPIO peripheral itself (always retained
   in EM2, per the reference manual's EM2 peripheral-retention table), not
   by anything EMU_EnterEM2()'s `restore` argument touches.

   Confirmed directly against the vendored em_emu.c: EMU_EnterEM2() sets
   SCB->SCR's SLEEPDEEP bit, then blocks on a real __WFI() (the
   ERRATA_FIX_EMU_E110_ENABLE/ERRATA_FIX_EMU_E220_DECBOD_ENABLE branches
   this part doesn't define both fall through to the plain `__WFI();` at
   the bottom of the #if chain), which is what actually blocks until an
   enabled interrupt -- RTC's COMP0 match, armed by
   platform_wakeup_timer_arm() before this is called, since NVIC_EnableIRQ(RTC_IRQn)
   was already done in efm32g210_rtc_init() -- wakes the core. Function
   returns after that WFI (plus its own post-wake bookkeeping), i.e. it
   really does block for the sleep duration rather than returning
   immediately.

   Critically, EMU_EnterEM2() sets SCB_SCR_SLEEPDEEP_Msk on entry and
   never clears it before returning (only the header's __STATIC_INLINE
   EMU_EnterEM1() clears it) -- the exact same hazard already hit and
   fixed on this repo's other two backends (platform_lpc810_power.c's
   platform_enter_low_power_sleep() sets SLEEPDEEP and never clears it;
   platform_stm32u031.c's HAL_PWREx_EnterSTOP2Mode() does the same).
   Without a fix, any later platform_wait_for_interrupt() call in the
   same power cycle (platform_efm32g210_i2c.c) would silently fall
   through to full EM2 deep sleep instead of a plain, light WFI wait --
   see the fix applied there, matching both other backends'
   platform_wait_for_interrupt() pattern. */
void platform_enter_low_power_sleep(void) {
    EMU_EnterEM2(true);
}
