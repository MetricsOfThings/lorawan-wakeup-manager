#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "vault/vault_log.h"
#include "stm32u0xx_hal.h"

#define MAIN_RAIL_EN_PORT GPIOA
#define MAIN_RAIL_EN_PIN  GPIO_PIN_0
#define I2C_SDA_PORT      GPIOA
#define I2C_SDA_PIN       GPIO_PIN_10
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_9

/* Defined in main.c; no shared header declares it (mirrors CubeMX's own
   convention of a project-wide but header-less SystemClock_Config()). */
void SystemClock_Config(void);

static I2C_HandleTypeDef s_i2c_handle;
static RTC_HandleTypeDef s_rtc_handle;

static void gpio_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = MAIN_RAIL_EN_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MAIN_RAIL_EN_PORT, &gpio_init);
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN, GPIO_PIN_RESET);
}

static void rtc_init(void);

void platform_init(void) {
    HAL_Init();
    gpio_init();
    rtc_init();

#ifdef VAULT_LOG_ENABLED
    extern void stm32u031_uart_init(void);
    stm32u031_uart_init();
#endif
    vault_log("platform_init: done\n");
}

void platform_main_rail_enable(bool on) {
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN,
                       on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void platform_bus_isolate(void) {
    /* Analog, no pull -- the STM32 equivalent of the LPC810 isolation
       step in platform_lpc810_gpio.c, and the exact mitigation the
       design spec (section 5 of the original MCU analysis report)
       calls for to prevent parasitic back-powering through ESD diodes. */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}

static void rtc_init(void) {
    /* The RTC lives in the backup domain, which is write-protected by
       default -- HAL_PWR_EnableBkUpAccess() must run before touching any
       RTC/backup-domain register (including its clock source select and
       __HAL_RCC_RTC_ENABLE()) or those writes silently have no effect.
       Verify against the STM32U031 reference manual's "Backup domain"
       section that DBP really does gate __HAL_RCC_RTC_ENABLE() the way
       it does on other STM32 families before flashing. */
    HAL_PWR_EnableBkUpAccess();

    /* Back on LSI. LSE was retried at both RCC_LSEDRIVE_LOW and
       RCC_LSEDRIVE_HIGH -- both produced UART output garbled from the
       very first character, every time, versus clean readable output
       under LSI every time. Ruling out drive strength as the cause
       (both levels failed identically) points at LSE itself being
       non-functional on this specific board -- a real hardware issue
       (crystal, load caps, or board layout) rather than something
       fixable from firmware. See git history for the full LSE <-> LSI
       investigation. Do not retry LSE again without first physically
       inspecting the crystal circuit (continuity, load caps, solder
       joints) or confirming oscillation with a scope -- further
       software-only drive-level tuning already been shown not to help. */
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
    /* AsynchPrediv=127, SynchPrediv=255 divide RTCCLK by (127+1)*(255+1)
       = 32768 to produce the 1 Hz SPRE clock platform_wakeup_timer_arm()
       assumes below -- these values were already chosen assuming
       exactly a 32.768 kHz RTCCLK, so switching to a real 32.768 kHz LSE
       crystal is what actually makes that assumption correct (with LSI,
       whose real frequency is nowhere near as tightly toleranced as a
       crystal, the same divide would have produced a 1 Hz clock only
       approximately, drifting with LSI's much larger tolerance). */
    s_rtc_handle.Init.AsynchPrediv = 127;
    s_rtc_handle.Init.SynchPrediv = 255;
    s_rtc_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&s_rtc_handle);

    /* Enable the RTC/TAMP combined NVIC line so RTC_TAMP_IRQHandler
       (below) actually fires when the wakeup timer elapses -- mirrors
       how platform_i2c_slave_init() enables I2C1_IRQn right after
       HAL_I2C_Init(). Without this, WKT-equivalent behavior on this
       backend would have the same "vectors into Default_Handler" bug
       Critical #1 fixed on the LPC810 side. RTC_TAMP_IRQn (IRQ 2) is the
       combined RTC+TAMP line per stm32u031xx.h -- this device does not
       split them onto separate NVIC lines the way some other STM32
       families do. */
    HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(RTC_TAMP_IRQn);
}

/* startup_stm32u031xx.s only weak-aliases SysTick_Handler to
   Default_Handler's silent infinite loop. HAL_Init() (platform_init()
   above) configures SysTick to fire every 1 ms via HAL_InitTick(), so
   without a real handler here, the first SysTick interrupt -- roughly
   1 ms after boot -- permanently hangs the CPU in that infinite loop.
   This was the actual root cause of firmware appearing to transmit only
   a handful of UART bytes before going silent: USART2's shift
   register/FIFO is a separate hardware block that kept draining
   whatever bytes were already queued at the moment the CPU hung, while
   the CPU itself never executed another instruction -- explaining why
   the cutoff was a fixed ~1 ms of transmission time (content-
   independent) rather than a fault or reset (RCC->CSR never showed a
   new reset cause because there wasn't one). This is the STM32
   convention CubeMX normally auto-generates into stm32u0xx_it.c; this
   firmware was hand-written without that file. HAL_IncTick() is what
   HAL_GetTick()/HAL_Delay() and all HAL timeout logic depend on. */
void SysTick_Handler(void) {
    HAL_IncTick();
}

void RTC_TAMP_IRQHandler(void) {
    /* HAL_RTCEx_WakeUpTimerIRQHandler() clears WUTF internally and then
       invokes HAL_RTCEx_WakeUpTimerEventCallback(), which
       platform_wakeup_timer_clear() below overrides -- verify that
       chain (in particular, that WUTF clearing happens before the
       callback and not after) against stm32u0xx_hal_rtc_ex.c before
       relying on it. */
    HAL_RTCEx_WakeUpTimerIRQHandler(&s_rtc_handle);
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc) {
    (void)hrtc;
    platform_wakeup_timer_clear();
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    /* RTC_WAKEUPCLOCK_CK_SPRE_16BITS drives the wakeup counter from the
       1 Hz SPRE clock (RTCCLK / (AsynchPrediv+1) / (SynchPrediv+1), see
       rtc_init() above), so reload value == seconds directly -- only
       exactly true while rtc_init() selects LSE (a real 32.768 kHz
       crystal). rtc_init() is currently back on LSI (see its comment --
       LSE was retried and ruled out as a firmware-fixable issue), whose
       frequency is nowhere near as tightly toleranced, so `seconds`
       only approximately matches real elapsed time until LSE is
       confirmed working again. Still verify the SPRE-clock derivation
       itself against the STM32U031 reference manual's RTC chapter
       before flashing. */
    HAL_RTCEx_DeactivateWakeUpTimer(&s_rtc_handle);
    HAL_RTCEx_SetWakeUpTimer_IT(&s_rtc_handle, seconds, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
}

void platform_wakeup_timer_clear(void) {
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&s_rtc_handle, RTC_FLAG_WUTF);
}

static void i2c_pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    /* GPIO_AF4_I2C1 -- verified against the vendored device header
       (stm32u0xx_hal_gpio_ex.h): STM32U031 only defines I2C1's
       alternate-function mapping as AF4 (the brief's guess of AF6 does
       not exist for this device). Also verify against the STM32U031
       datasheet's alternate-function table for whichever pins the real
       schematic actually uses. */
    gpio_init.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}

static void i2c1_clock_source_init(void) {
    /* I2C1's kernel clock defaults to PCLK1, which tracks the low-power
       main clock (MSI, kept slow for power per SystemClock_Config()) --
       far too slow to reach 400 kHz Fast-mode. Switching I2C1's kernel
       clock source to HSI (16 MHz, fixed) decouples I2C bus timing from
       the main clock choice, matching how the RTC's clock source is
       already selected independently in rtc_init() above. HSI must be
       explicitly turned on first: unlike MSI it is not guaranteed
       running at this point since SystemClock_Config() only configures
       MSI. __HAL_RCC_HSI_ENABLE() only sets HSION -- it does not wait
       for the oscillator to actually stabilize, so without the
       HSIRDY spin-wait below, HAL_I2C_Init() a few lines down could
       start clocking I2C1 from HSI before it's actually running,
       corrupting the timing of whichever I2C transaction happens to
       land first after each wake. Verify HSI's real startup time (and
       thus whether this wait is ever observable in practice) against
       the STM32U031 datasheet's oscillator characteristics table. */
    __HAL_RCC_HSI_ENABLE();
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY)) {
        /* Busy-wait for HSI to stabilize. */
    }

    RCC_PeriphCLKInitTypeDef periph_clk_init = {0};
    periph_clk_init.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    periph_clk_init.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
    HAL_RCCEx_PeriphCLKConfig(&periph_clk_init);
}

void platform_i2c_slave_init(uint8_t addr) {
    i2c1_clock_source_init();
    __HAL_RCC_I2C1_CLK_ENABLE();
    i2c_pins_init();

    s_i2c_handle.Instance = I2C1;
    /* Fast-mode (400 kHz) at a 16 MHz I2C kernel clock (HSI, selected
       above), computed by hand from ST's TIMINGR formula
       (t_SCL = t_SYNC1 + t_SYNC2 + [(SCLH+1)+(SCLL+1)] x (PRESC+1) x
       t_I2CCLK) rather than taken from a verified CubeMX/datasheet
       table -- PRESC=3, SCLDEL=2, SDADEL=0, SCLH=3, SCLL=6 gives
       t_I2CCLK=62.5ns, t_PRESC=250ns, SCLH=1000ns, SCLL=1750ns, for an
       estimated ~348 kHz actual SCL (safely under the 400 kHz Fast-mode
       ceiling, assuming a ~125 ns SYNC1+SYNC2 analog-filter delay this
       hand calculation can't measure precisely). Re-derive and confirm
       this value with CubeMX's timing calculator or a scope before
       flashing -- same caveat this file already applies to the 100 kHz
       constant this replaces. */
    s_i2c_handle.Init.Timing = 0x30200306;
    s_i2c_handle.Init.OwnAddress1 = (uint32_t)(addr << 1);
    s_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c_handle.Init.OwnAddress2 = 0;
    s_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&s_i2c_handle);

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
    /* STM32U031's I2C1_IRQn (IRQ 23) is a single combined line for both
       event and error interrupts (see stm32u031xx.h: "I2C1 global
       interrupt"), unlike larger STM32 families that split I2Cx_EV and
       I2Cx_ER onto separate NVIC lines. Both HAL sub-handlers must be
       serviced from this one ISR or NACK/bus-error conditions raised
       while listen mode is enabled would never be handled. */
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

void platform_enter_low_power_sleep(void) {
    /* Matches the analysis report's own example loop: disable I2C
       (already done via platform_i2c_slave_deinit() before this is
       called), suspend SysTick so it can't wake the core prematurely,
       enter Stop 2, then resume SysTick and reconfigure the system
       clock on return -- HAL_PWREx_EnterSTOP2Mode() blocks until an
       enabled wakeup source (the RTC wakeup timer, armed by
       platform_wakeup_timer_arm() before this is called) fires, and
       returns with CPU registers and SRAM intact, consistent with
       vault_core's resume-in-place assumption (spec section 6). */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
    HAL_ResumeTick();
    SystemClock_Config();
}

void platform_wait_for_interrupt(void) {
    /* HAL_PWREx_EnterSTOP2Mode() (above) sets SCB->SCR's SLEEPDEEP bit
       and never clears it -- confirmed directly in
       stm32u0xx_hal_pwr_ex.c. Without explicitly clearing it here, any
       call to this function after the first full Stop 2 cycle would
       silently fall through to Stop 2 instead of plain Sleep mode (CPU
       clock only, everything else -- including I2C1 -- still running
       and able to service the interrupt this is waiting for). Plain
       __WFI() with SLEEPDEEP clear halts only the CPU core clock until
       any enabled interrupt fires. */
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
    __WFI();
}
