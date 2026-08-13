#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "stm32c0xx_hal.h"

/* --- SO8N pin map for STM32C011J6M6 -- provenance and status ---------
   Task 1 (see .superpowers/sdd/task-1-report.md) discovered this part's
   SO8N package is NOT wired like every other backend in this project:
   4 of its 8 physical pins are software-selectable via the SYSCFG_CFGR3
   pin-multiplexer register (HAL_SYSCFG_SetPinBinding() /
   HAL_SYSCFG_GetPinBinding(), confirmed real in the vendored
   stm32c0xx_hal.h / stm32c0xx_ll_system.h), rather than fixed in
   silicon the way e.g. the EFM32G210 or STM32U031 backends' pins are.

   Confirmed reset-default bindings (PINMUXn bits = 0 at power-on, no
   HAL_SYSCFG_SetPinBinding() call needed to have them active):
     Physical pin 1 -> PB7  (default; PC14 is the alternate)
     Physical pin 4 -> PF2  (default; PA0/PA1/PA2 are alternates)
     Physical pin 5 -> PA8  (default; PA11 is the alternate)
     Physical pin 8 -> PA14 (default; PB6/PC15 are alternates)

   Physical pins 2/3/6/7 are NOT in the SYSCFG_CFGR3 binding table --
   Task 1 inferred (but did NOT independently confirm from a datasheet)
   that these are fixed VDD/VSS/NRST/SWDIO, consistent with an 8-pin
   power+debug+minimal-GPIO part. In particular, pin 7's identity as
   PA13/SWDIO is NOT yet confirmed against a datasheet -- Task 7 (debug
   UART, which repurposes SWDIO for UART TX) MUST verify this itself
   before relying on it; do not silently inherit this assumption.
   NRST (physical pin 4, PF2-NRST default), SWDIO (PA13, physical
   pin 7) and SWCLK (PA14-BOOT0 default, physical pin 8) are otherwise
   untouched by this fix.

   *** CORRECTED against the real STM32C011x4/x6 datasheet (DS13866) ***
   Task 1/an earlier pass on this file picked PF2 (physical pin 4) for
   I2C SDA and PA8 (physical pin 5, its SYSCFG_CFGR3 default binding)
   for I2C SCL, flagged PROVISIONAL pending a real datasheet check.
   That check (DS13866 Table 12 "Pin assignment and description" plus
   its alternate-function tables) now shows NEITHER pin actually
   carries an I2C1 alternate function: PF2's only AFs are MCO and
   TIM1_CH4; PA8's are MCO/USART2_TX/TIM1_CH1/etc. Both guesses were
   wrong and would not have worked on real silicon.

   The datasheet's own bootloader section corroborates the correct
   pair independently, stating the I2C bootloader runs "I2C-bus on
   pins PB6/PB7" for parts that expose them directly -- but on THIS
   SO8N package PB6/PB7 aren't both free (PB7 is needed for
   MAIN_RAIL_EN below, and PB6 isn't broken out on physical pin 5/6 at
   all), so this part's I2C1 has to be reached via a different, less
   obvious route: physical pins 5 and 6 combine TWO independent SYSCFG
   mechanisms to reach PA9/PA10 (the pins that actually carry
   I2C1_SCL/I2C1_SDA, AF6, per DS13866's AF table):

     1. SYSCFG_CFGR3 "SO8N pin binding" (HAL_SYSCFG_SetPinBinding(),
        same mechanism as the reset-default bindings above): physical
        pin 5 defaults to PA8, but can instead be bound to PA11 via
        HAL_BIND_SO8_PIN5_PA11 (confirmed in the vendored
        stm32c0xx_hal.h / stm32c0xx_ll_system.h). Physical pin 6 has
        no SYSCFG_CFGR3 choice at all -- it is always bonded as PA12.

     2. SYSCFG_CFGR1's separate "PA9/PA10 remapped in place of
        PA11/PA12" mechanism -- a DIFFERENT register from CFGR3, not
        used anywhere else in this codebase. Confirmed in the vendored
        headers: SYSCFG_CFGR1_PA11_RMP / SYSCFG_CFGR1_PA12_RMP bits in
        stm32c011xx.h, exposed via HAL_SYSCFG_EnableRemap(uint32_t
        PinRemap) (stm32c0xx_hal.h, implemented on top of
        LL_SYSCFG_EnablePinRemap() in stm32c0xx_ll_system.h) taking
        SYSCFG_REMAP_PA11 / SYSCFG_REMAP_PA12 (stm32c0xx_hal.h's own
        comments: "PA11 pad behaves digitally as PA9 GPIO pin" /
        "PA12 pad behaves digitally as PA10 GPIO pin" -- i.e. once
        bound to PA11/PA12 by CFGR3, CFGR1's remap bit makes that same
        pad present PA9's / PA10's alternate-function identity,
        including PA9's/PA10's I2C1 AF6).

   Applying both to this part's two I2C pins:
     Physical pin 5: CFGR3 bound to PA11 (HAL_BIND_SO8_PIN5_PA11) +
       CFGR1 SYSCFG_REMAP_PA11 -> presents as PA9 -> I2C1_SCL (AF6).
     Physical pin 6: always PA12 (no CFGR3 choice needed) + CFGR1
       SYSCFG_REMAP_PA12 -> presents as PA10 -> I2C1_SDA (AF6).
   Do not swap these: DS13866's AF table lists I2C1_SCL under PA9 and
   I2C1_SDA under PA10 specifically -- SCL/SDA are not interchangeable
   just because both remap through the same CFGR1 mechanism.

   Both SYSCFG calls are made once, early, in gpio_init() (see below),
   since they must be in effect before I2C peripheral init (Task 5)
   configures these pins' alternate function.

   Concrete pin decision made here:
     MAIN_RAIL_EN = PB7  (physical pin 1, default binding). CONFIRMED
       SAFE: a plain GPIO output needs no alternate-function
       capability or SYSCFG remap, so this assignment never depended
       on the I2C AF question above.
     I2C SCL = PA9  (physical pin 5, remapped via SYSCFG_CFGR3 +
       SYSCFG_CFGR1 as described above). CONFIRMED against DS13866.
     I2C SDA = PA10 (physical pin 6, remapped via SYSCFG_CFGR1 alone,
       since pin 6 has no CFGR3 binding choice). CONFIRMED against
       DS13866.
   ----------------------------------------------------------------- */
#define MAIN_RAIL_EN_PORT GPIOB
#define MAIN_RAIL_EN_PIN  GPIO_PIN_7
#define I2C_SDA_PORT      GPIOA
#define I2C_SDA_PIN       GPIO_PIN_10
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_9

/* platform_wakeup_timer_arm() below implements "fire in `seconds` seconds"
   as a date-masked Alarm A match on hh:mm:ss (see that function's comment
   for why) -- it computes `(now_of_day + seconds) % 86400`, which folds
   seconds >= 86400 back into a smaller time-of-day than intended and
   silently produces a SHORTER wait than requested (in the worst case,
   seconds == 86400 collides with "right now" and the alarm fires almost
   immediately instead of a full day later). 86399 is the largest value
   for which that modulo still lands on a distinct, strictly-in-the-future
   time-of-day relative to `now_of_day` (landing one second short of it,
   never on top of it), so it is the true safe ceiling -- not 86400.
   Clamping here errs toward waking sooner than requested, never later,
   which is the safe direction for a wake timer. */
#define STM32C011_WAKEUP_TIMER_MAX_SECONDS 86399u

static RTC_HandleTypeDef s_rtc_handle;

static void gpio_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Bind physical pin 5 to PA11 and physical pin 6 (always PA12) to
       PA10's remapped identity, then remap both through SYSCFG_CFGR1 so
       they present as PA9/PA10 -- the pins that actually carry I2C1's
       alternate function (AF6) per DS13866. See the pin-map comment
       above MAIN_RAIL_EN_PORT for the full derivation. This must run
       before anything below (or later, Task 5's I2C peripheral init)
       configures I2C_SCL_PORT/I2C_SDA_PORT's alternate function, since
       the pad doesn't present PA9/PA10's identity -- and therefore
       doesn't accept AF6 -- until these SYSCFG bits are set. SYSCFG's
       own clock must be enabled first, or CFGR1/CFGR3 writes are
       ignored. */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_SYSCFG_SetPinBinding(HAL_BIND_SO8_PIN5_PA11);
    HAL_SYSCFG_EnableRemap(SYSCFG_REMAP_PA11 | SYSCFG_REMAP_PA12);

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = MAIN_RAIL_EN_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MAIN_RAIL_EN_PORT, &gpio_init);
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN, GPIO_PIN_RESET);
}

void platform_main_rail_enable(bool on) {
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN,
                       on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void platform_bus_isolate(void) {
    /* Analog, no pull -- same parasitic-back-powering mitigation every
       other backend's platform_bus_isolate() already applies (design
       spec, MCU_Analysis_Report.md section 5). SDA (PA10) and SCL
       (PA9) are both on GPIOA here (see the pin-map comment above
       MAIN_RAIL_EN_PORT for how physical pins 5/6 reach these via
       SYSCFG_CFGR3 pin binding + SYSCFG_CFGR1 remap), so a single
       clock enable and two HAL_GPIO_Init() calls (one per pin) cover
       both. */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;

    gpio_init.Pin = I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);

    gpio_init.Pin = I2C_SCL_PIN;
    HAL_GPIO_Init(I2C_SCL_PORT, &gpio_init);
}

static void rtc_init(void);

void platform_init(void) {
    HAL_Init();
    gpio_init();
    /* No explicit HAL_RCC_ClockConfig() / SystemClock_Config() call is
       needed here, unlike STM32U031's MSI-based tree: RCC_CFGR's SW
       field (system clock source select) is 0b00 == HSI at reset
       (confirmed via RCC_SYSCLKSOURCE_HSI == 0x0 /
       RCC_SYSCLKSOURCE_STATUS_HSI == 0x0 in the vendored
       stm32c0xx_hal_rcc.h, i.e. HSI is both the encoded value AND the
       all-zero reset value of CFGR.SW), and HSISYS (the divided HSI
       feeding SYSCLK) is likewise already enabled and selected out of
       reset per the STM32C0 reference manual's RCC chapter. HSI needs
       no PLL, trimming, or wait-for-ready spin here: it's already the
       running, stable system clock the moment this line executes.
       Task 7 (debug UART) appends its own init call here -- do not add
       speculative calls to functions that don't exist yet. */
    rtc_init();
}

/* --- RTC wake timer -- provenance and status --------------------------
   The brief this task started from (modeled on platform_stm32u031.c's
   rtc_init()) assumed this device's RTC exposes a WakeUpTimer, the way
   STM32U031's does (HAL_RTCEx_SetWakeUpTimer_IT() etc.). That assumption
   does NOT hold here: grepping the vendored
   stm32c0xx_hal_rtc_ex.h/.c for WakeUpTimer/WAKEUPCLOCK turns up
   nothing, and RTC_TypeDef in stm32c011xx.h has no WUTR register at
   all -- STM32C011's RTC IP block is a cut-down variant with only Alarm
   A (ALRMAR/ALRMASSR) and a TimeStamp function, no WakeUpTimer, no Alarm
   B, no backup registers of its own. platform_wakeup_timer_arm() below
   is therefore built on HAL_RTC_SetAlarm_IT()/Alarm A instead -- the
   design spec doc (2026-08-13-stm32c011-backend-design.md, "RTC wake
   timer" section) already flagged "or equivalent compare/alarm
   mechanism" as the fallback to confirm, so this is not a deviation from
   plan, just resolving that open question the way the hardware actually
   requires.

   Also NOT needed here, despite the brief's illustrative code copying it
   from STM32U031: HAL_PWR_EnableBkUpAccess(). It does not exist anywhere
   in the vendored STM32C0 HAL (grepped stm32c0xx_hal_pwr.h/.c for
   EnableBkUpAccess/DisableBkUpAccess -- no match), and PWR_TypeDef in
   stm32c011xx.h has no DBP-style write-protect bit either. Consistent
   with that: RTC's enable bit and clock-source select live in RCC->CSR1
   (RCC_CSR1_RTCEN / RCC_CSR1_RTCSEL) on this family, not in a
   battery-backed RCC->BDCR the way STM32U031's do -- STM32C011 (8-pin
   SO8N, no VBAT pin per Task 1's pin-map findings) has no separate
   backup domain to write-protect in the first place. */
static void rtc_init(void) {
    RCC_OscInitTypeDef lsi_osc_init = {0};
    lsi_osc_init.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    lsi_osc_init.LSIState = RCC_LSI_ON;
    HAL_RCC_OscConfig(&lsi_osc_init);

    RCC_PeriphCLKInitTypeDef periph_clk_init = {0};
    periph_clk_init.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph_clk_init.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    HAL_RCCEx_PeriphCLKConfig(&periph_clk_init);

    __HAL_RCC_RTC_ENABLE();
    s_rtc_handle.Instance = RTC;
    s_rtc_handle.Init.HourFormat = RTC_HOURFORMAT_24;
    /* AsynchPrediv=124, SynchPrediv=255 divide RTCCLK by (124+1)*(255+1)
       = 32000 to produce a 1 Hz ck_spre (the clock that increments the
       calendar's seconds field, and against which Alarm A's TR compare
       ultimately runs) -- chosen from LSI's REAL nominal frequency on
       this part, not the brief's illustrative 127/255 (which divides by
       32768, assuming an LSE-crystal-like 32.768 kHz source).
       LSI_VALUE == 32000 Hz is confirmed directly in this project's own
       platform/stm32c011/include/stm32c0xx_hal_conf.h (and matches the
       vendored stm32c0xx_hal_conf_template.h default) -- and, contrary
       to what the brief's comment speculated, STM32U031's LSI_VALUE
       (platform/stm32u031/include/stm32u0xx_hal_conf.h) is ALSO 32000 Hz
       nominal, i.e. the two parts' LSI's are not actually a "different
       nominal value" from each other. 32000 = 125 x 256 factors exactly,
       so this prediv choice gives a ck_spre that is exactly 1 Hz against
       the nominal LSI frequency (versus the brief's 32768-based values,
       which would run about 2.4% fast against a real 32000 Hz LSI) --
       though actual LSI frequency still carries LSI's usual wide
       temperature/voltage tolerance (unlike a crystal), so
       platform_wakeup_timer_arm()'s `seconds` argument is only as
       accurate as LSI itself, same caveat STM32U031's rtc_init() already
       documents for its own LSI fallback path. */
    s_rtc_handle.Init.AsynchPrediv = 124;
    s_rtc_handle.Init.SynchPrediv = 255;
    s_rtc_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&s_rtc_handle);

    /* RTC_IRQn (IRQ 2) is this family's ONLY RTC-related NVIC line --
       confirmed directly in stm32c011xx.h's IRQn_Type enum ("RTC
       interrupt through the EXTI line 19 & 21"), unlike STM32U031's
       combined RTC_TAMP_IRQn (this device has no TAMP peripheral at
       all). The vendored startup_stm32c011xx.s vector table names the
       handler RTC_IRQHandler (see its .word entry and weak-alias
       default), not RTC_TAMP_IRQHandler -- matched below.
       HAL_RTC_SetAlarm_IT() (called from platform_wakeup_timer_arm())
       enables the underlying EXTI line 19 alarm-event interrupt itself
       (__HAL_RTC_ALARM_EXTI_ENABLE_IT(), confirmed in
       stm32c0xx_hal_rtc.c), so only the NVIC line itself needs enabling
       here. */
    HAL_NVIC_SetPriority(RTC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    /* Clamp before anything else -- see STM32C011_WAKEUP_TIMER_MAX_SECONDS'
       comment above for why 86400+ can't just be passed through to the
       modulo arithmetic below. */
    if (seconds > STM32C011_WAKEUP_TIMER_MAX_SECONDS) {
        seconds = STM32C011_WAKEUP_TIMER_MAX_SECONDS;
    }

    HAL_RTC_DeactivateAlarm(&s_rtc_handle, RTC_ALARM_A);

    RTC_TimeTypeDef now_time = {0};
    RTC_DateTypeDef now_date = {0};
    HAL_RTC_GetTime(&s_rtc_handle, &now_time, RTC_FORMAT_BIN);
    /* HAL_RTC_GetTime() latches the calendar's shadow registers on every
       STM32 family that has them (including this one); HAL_RTC_GetDate()
       must be called immediately after -- even though the date fields
       below go unused -- purely to unlatch them again for the next read.
       Skipping it would leave TR/DR reads returning a stale snapshot
       after the first call. */
    HAL_RTC_GetDate(&s_rtc_handle, &now_date, RTC_FORMAT_BIN);

    /* This device's RTC has no WakeUpTimer (see rtc_init()'s comment),
       so "fire in `seconds` seconds" is implemented as an Alarm A set to
       the wall-clock time `seconds` from now, with AlarmMask masking out
       the date/weekday field entirely -- meaning Alarm A fires the next
       time the hh:mm:ss fields match, regardless of what day it is. That
       naturally handles wraparound past midnight for any `seconds` value
       up to just under 24h (86400s) without any date-field arithmetic;
       it does NOT distinguish "86400s from now" from "right now" or
       correctly handle `seconds` spanning more than one day, a ceiling
       consistent with the other backends in this project (STM32U031's
       16-bit CK_SPRE WakeUpTimer reload maxes out around 65535s, ~18.2h,
       for the same underlying reason: none of the wake intervals this
       vault firmware actually uses are anywhere near a full day). */
    uint32_t now_of_day = (uint32_t)now_time.Hours * 3600u
                         + (uint32_t)now_time.Minutes * 60u
                         + (uint32_t)now_time.Seconds;
    uint32_t target_of_day = (now_of_day + seconds) % 86400u;

    RTC_AlarmTypeDef alarm = {0};
    alarm.AlarmTime.Hours = (uint8_t)(target_of_day / 3600u);
    alarm.AlarmTime.Minutes = (uint8_t)((target_of_day % 3600u) / 60u);
    alarm.AlarmTime.Seconds = (uint8_t)(target_of_day % 60u);
    alarm.AlarmTime.SubSeconds = 0;
    alarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
    alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    alarm.AlarmDateWeekDay = 1;
    alarm.Alarm = RTC_ALARM_A;

    HAL_RTC_SetAlarm_IT(&s_rtc_handle, &alarm, RTC_FORMAT_BIN);
}

void platform_wakeup_timer_clear(void) {
    __HAL_RTC_ALARM_CLEAR_FLAG(&s_rtc_handle, RTC_FLAG_ALRAF);
}

void RTC_IRQHandler(void) {
    /* Handler name matches startup_stm32c011xx.s's vector table entry
       for RTC_IRQn -- see rtc_init()'s comment on why this is
       RTC_IRQHandler and not RTC_TAMP_IRQHandler on this family. */
    HAL_RTC_AlarmIRQHandler(&s_rtc_handle);
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc) {
    (void)hrtc;
    platform_wakeup_timer_clear();
}

/* --- I2C1 slave driver (HAL listen-mode) -- provenance and status ----
   Follows platform_stm32u031.c's i2c_pins_init()/i2c1_clock_source_init()/
   platform_i2c_slave_init()/I2C1_IRQHandler()/HAL_I2C_*Callback() shape
   directly (same HAL module family, same listen-mode API surface), but
   every part-specific constant below was re-derived against this part's
   real vendored headers rather than copied from STM32U031:

   1. Pins/AF: I2C_SCL_PIN/I2C_SCL_PORT and I2C_SDA_PIN/I2C_SDA_PORT
      (PA9/PA10) and the SYSCFG_CFGR3/CFGR1 remap that makes them live are
      already established above (see the pin-map comment near
      MAIN_RAIL_EN_PORT) and already run in gpio_init(), which platform_init()
      calls before rtc_init()/anything here. GPIO_AF6_I2C1 (0x06) is
      confirmed directly in the vendored stm32c0xx_hal_gpio_ex.h -- this
      happens to be the same AF number STM32U031's brief guessed for its
      own part (AF6), but that was NOT assumed here; DS13866's own AF
      table (see the pin-map comment) already independently confirmed AF6
      for PA9/PA10 on this device.

   2. Peripheral instance/IRQ: I2C1 is this part's only I2C instance
      (I2C1_BASE / I2C1 confirmed in stm32c011xx.h). Its NVIC line,
      I2C1_IRQn = 23 ("I2C1 Interrupt (combined with EXTI 23)" per
      stm32c011xx.h's IRQn_Type enum), is -- like STM32U031's I2C1_IRQn --
      a single combined line for both event and error interrupts; both
      HAL sub-handlers are serviced from one ISR below, same as
      STM32U031's I2C1_IRQHandler().

   3. Kernel clock source: this is a REAL divergence from STM32U031's HAL,
      not just a renamed constant. STM32C0's RCC_PeriphCLKInitTypeDef
      (stm32c0xx_hal_rcc_ex.h) offers only RCC_I2C1CLKSOURCE_PCLK1,
      RCC_I2C1CLKSOURCE_SYSCLK, or RCC_I2C1CLKSOURCE_HSIKER for I2c1ClockSelection
      -- there is no RCC_I2C1CLKSOURCE_HSI on this part (the brief's
      illustrative code, copied from STM32U031's own constant name, does
      not compile here). "HSIKER" is a second, independent output of the
      same HSI oscillator (distinct from HSISYS, the divided output that
      feeds the CPU/AHB/APB tree) intended specifically for kernel
      peripherals like I2C/USART -- see stm32c0xx_hal_rcc.h's
      __HAL_RCC_HSISTOP_ENABLE()/RCC_CR_HSIKERON comment: HSIKERON only
      forces HSI to stay running as a kernel-clock source *during STOP
      mode* ("has not effect on the HSION bit"), so in normal run mode
      (the only time this driver is active -- platform_i2c_slave_deinit()
      already runs before platform_enter_low_power_sleep() enters Stop 2,
      per platform_stm32u031.c's own platform_enter_low_power_sleep()
      comment, which this part's Task 6 will mirror), HSIKER is already
      available the moment HSION is set -- which platform_init() already
      guarantees implicitly (HSI is the reset-default, already-running
      SYSCLK source, see platform_init()'s comment) and this function
      re-confirms explicitly/defensively exactly as STM32U031's
      i2c1_clock_source_init() does. HSIKER's divider (RCC_CR_HSIKERDIV,
      configured via a *separate* RCC_PERIPHCLK_HSIKER selector this
      driver does not set) resets to RCC_HSIKER_DIV1 (divide-by-1,
      confirmed 0x0 in stm32c0xx_hal_rcc_ex.h), so HSIKER runs at the full,
      undivided HSI_VALUE (48 MHz, confirmed in
      platform/stm32c011/include/stm32c0xx_hal_conf.h) without this driver
      needing to touch RCC_PERIPHCLK_HSIKER/HSIKerClockDivider at all.

   4. TIMING: STM32U031's 0x30200306 assumed a 16 MHz I2C kernel clock
      (t_I2CCLK = 62.5 ns) and picked PRESC=3 (t_PRESC = (PRESC+1) x
      t_I2CCLK = 250 ns) so that SCLDEL=2, SDADEL=0, SCLH=3, SCLL=6 give
      t_SCLH=1000 ns / t_SCLL=1750 ns (~348 kHz actual SCL, safely under
      400 kHz Fast-mode). This part's I2C1 kernel clock is HSIKER at
      48 MHz (t_I2CCLK = 20.833 ns, 3x faster than STM32U031's 16 MHz),
      so reusing PRESC=3 would make t_PRESC three times too short and
      overshoot 400 kHz. Scaling PRESC to hold t_PRESC constant at 250 ns
      instead: (PRESC+1) = 250 ns / 20.833 ns = 12, so PRESC=11 (0xB, vs.
      STM32U031's 3) -- keeping SCLDEL/SDADEL/SCLH/SCLL identical to
      STM32U031's values reproduces the exact same t_SCLH/t_SCLL/t_SCL
      (~348 kHz) on this part's faster clock. This gives
      TIMING = 0xB0200306 (only the top PRESC nibble changed from
      STM32U031's 0x3 to 0xB; SCLDEL/SDADEL/SCLH/SCLL are unchanged).
      Same caveat STM32U031's file already documents: this is a hand
      derivation from ST's TIMINGR formula assuming a small, unmeasured
      SYNC1+SYNC2 analog-filter delay -- re-derive/confirm with CubeMX's
      timing calculator or a scope before flashing.
   ----------------------------------------------------------------- */

static I2C_HandleTypeDef s_i2c_handle;

static void i2c_pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    /* GPIO_AF6_I2C1 -- confirmed against the vendored device header
       (stm32c0xx_hal_gpio_ex.h) and against DS13866's own AF table for
       PA9/PA10 (see the pin-map comment above MAIN_RAIL_EN_PORT). The
       SYSCFG_CFGR3 pin-binding + SYSCFG_CFGR1 remap that makes physical
       pins 5/6 present as PA9/PA10 in the first place already ran in
       gpio_init(), before platform_i2c_slave_init() (and therefore this
       function) runs. */
    gpio_init.Alternate = GPIO_AF6_I2C1;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}

static void i2c1_clock_source_init(void) {
    /* Defensive/explicit, mirroring platform_stm32u031.c's
       i2c1_clock_source_init() -- HSI (and therefore HSIKER, see the
       provenance comment above) is already the running, stable,
       reset-default system clock by the time this runs (platform_init()'s
       own comment), so this spin-wait is not expected to ever actually
       block, but costs nothing to keep as a safety net against a future
       change to platform_init()'s clock setup. */
    __HAL_RCC_HSI_ENABLE();
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY)) {
        /* Busy-wait for HSI to stabilize. */
    }

    RCC_PeriphCLKInitTypeDef periph_clk_init = {0};
    periph_clk_init.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    /* RCC_I2C1CLKSOURCE_HSIKER, not STM32U031's RCC_I2C1CLKSOURCE_HSI --
       this part's I2C1 kernel clock source enum only offers PCLK1,
       SYSCLK, or HSIKER (confirmed in stm32c0xx_hal_rcc_ex.h); see the
       provenance comment above for what HSIKER actually is on this
       family. */
    periph_clk_init.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSIKER;
    HAL_RCCEx_PeriphCLKConfig(&periph_clk_init);
}

void platform_i2c_slave_init(uint8_t addr) {
    i2c1_clock_source_init();
    __HAL_RCC_I2C1_CLK_ENABLE();
    i2c_pins_init();

    s_i2c_handle.Instance = I2C1;
    /* Fast-mode (400 kHz) TIMING value re-derived for this part's 48 MHz
       HSIKER I2C1 kernel clock -- see the provenance comment above this
       section for the full PRESC re-derivation from STM32U031's 16 MHz
       constant. */
    s_i2c_handle.Init.Timing = 0xB0200306;
    s_i2c_handle.Init.OwnAddress1 = (uint32_t)(addr << 1);
    s_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c_handle.Init.OwnAddress2 = 0;
    s_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&s_i2c_handle);

    /* I2C1_IRQn = 23 is this family's single combined event+error NVIC
       line for I2C1 (confirmed in stm32c011xx.h's IRQn_Type enum) -- same
       topology as STM32U031's I2C1_IRQn, unlike STM32 families that split
       I2Cx_EV/I2Cx_ER onto separate lines. */
    HAL_NVIC_SetPriority(I2C1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C1_IRQn);

    HAL_I2C_EnableListen_IT(&s_i2c_handle);
}

void platform_i2c_slave_deinit(void) {
    HAL_NVIC_DisableIRQ(I2C1_IRQn);
    HAL_I2C_DisableListen_IT(&s_i2c_handle);
    HAL_I2C_DeInit(&s_i2c_handle);
    __HAL_RCC_I2C1_CLK_DISABLE();
}

void I2C1_IRQHandler(void) {
    /* Single combined event+error NVIC line -- see platform_i2c_slave_init()'s
       comment above. Both HAL sub-handlers must be serviced from this one
       ISR or NACK/bus-error conditions raised while listen mode is
       enabled would never be handled. */
    HAL_I2C_EV_IRQHandler(&s_i2c_handle);
    HAL_I2C_ER_IRQHandler(&s_i2c_handle);
}

static uint8_t s_rx_byte;
static uint8_t s_tx_byte;

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t direction, uint16_t addr_match_code) {
    (void)addr_match_code;
    if (direction == I2C_DIRECTION_TRANSMIT) {
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx_byte, 1, I2C_NEXT_FRAME);
    } else {
        s_tx_byte = vault_i2c_registers_on_read_request();
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    vault_i2c_registers_on_write_byte(s_rx_byte);
    HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    s_tx_byte = vault_i2c_registers_on_read_request();
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    vault_i2c_registers_on_stop();
    HAL_I2C_EnableListen_IT(hi2c);
}
