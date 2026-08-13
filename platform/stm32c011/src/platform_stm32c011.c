#include "vault/platform.h"
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

   This file deliberately uses ONLY the reset-default bindings above,
   so it needs zero HAL_SYSCFG_SetPinBinding() calls -- no runtime
   pin remapping means nothing to get wrong at boot on an 8-pin part
   where a wrong guess could short two signals together on real
   hardware.

   Physical pins 2/3/6/7 are NOT in the SYSCFG_CFGR3 binding table --
   Task 1 inferred (but did NOT independently confirm from a datasheet)
   that these are fixed VDD/VSS/NRST/SWDIO, consistent with an 8-pin
   power+debug+minimal-GPIO part. In particular, pin 7's identity as
   PA13/SWDIO is NOT yet confirmed against a datasheet -- Task 7 (debug
   UART, which repurposes SWDIO for UART TX) MUST verify this itself
   before relying on it; do not silently inherit this assumption.

   Concrete pin decision made here:
     MAIN_RAIL_EN = PB7  (physical pin 1, default binding). CONFIRMED
       SAFE for this task: a plain GPIO output needs no
       alternate-function capability, so this assignment does not
       depend on the still-open AF-capability question below.
     I2C SDA = PF2  (physical pin 4, default binding) -- PROVISIONAL.
       Task 5 (I2C driver) must independently confirm PF2 actually
       supports I2C1's alternate function in the vendored
       stm32c0xx_hal_gpio_ex.h before treating this as final. It is
       only used here for platform_bus_isolate()'s GPIO_MODE_ANALOG
       reconfiguration, which -- like the GPIO output above -- needs no
       AF capability either, so this pin name is safe to use for that
       purpose regardless of how the AF question resolves.
     I2C SCL = PA8  (physical pin 5, default binding) -- same
       PROVISIONAL status and same reasoning as I2C SDA above.
   ----------------------------------------------------------------- */
#define MAIN_RAIL_EN_PORT GPIOB
#define MAIN_RAIL_EN_PIN  GPIO_PIN_7
#define I2C_SDA_PORT      GPIOF
#define I2C_SDA_PIN       GPIO_PIN_2
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_8

static RTC_HandleTypeDef s_rtc_handle;

static void gpio_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

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
       spec, MCU_Analysis_Report.md section 5). SDA (PF2) and SCL (PA8)
       are on different ports here (unlike other backends, since this
       part's SO8N pinout is software-selectable rather than a fixed
       table -- see the pin-map comment above), so each port needs its
       own clock enable and its own HAL_GPIO_Init() call. */
    __HAL_RCC_GPIOF_CLK_ENABLE();
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
