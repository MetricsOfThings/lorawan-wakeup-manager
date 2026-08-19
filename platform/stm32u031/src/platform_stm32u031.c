#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "vault/vault_log.h"
#include "stm32u0xx_hal.h"

#define MAIN_RAIL_EN_PORT GPIOA
#define MAIN_RAIL_EN_PIN  GPIO_PIN_0
/* I2C2 on PA6/PA7 (physical pins 11/12), not I2C1 on PA9/PA10 (physical
   pins 14/15 via the PA11 remap) -- real hardware testing (single-wire
   injection: drive only SDA with SCL fully disconnected, observe the
   same signal appear on both pins with no external connection at all,
   then repeat driving only SCL) proved PA9/PA10/PA11/PA12 are
   electrically tied together on this specific chip/package, contrary
   to what the datasheet's documented SYSCFG_CFGR1 remap register
   implies. I2C2 is a genuinely separate peripheral instance with its
   own register block (confirmed against stm32u031xx.h: I2C2_BASE,
   distinct from I2C1_BASE) on pins with no involvement in that tied
   cluster at all. PA6/PA7 do share their own physical pins (11/12)
   with PA5/PB0 respectively per the datasheet, but that sharing is
   undocumented as a runtime-selectable mechanism (no footnote, unlike
   PA9-12's explicit remap) -- verify with the same single-wire
   injection test before trusting it. */
#define I2C_SDA_PORT      GPIOA
#define I2C_SDA_PIN       GPIO_PIN_6
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_7

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
       how platform_i2c_slave_init() enables I2C2_3_IRQn right after
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
    /* Previously left unset, defaulting to GPIO_SPEED_FREQ_LOW (0x0) --
       a real, verified gap (docs/stm32u031_i2c_slave_analysis.md issue
       #6): low GPIO output/Schmitt-trigger speed can affect edge
       detection reliability on the receive path, exactly where the
       COMMAND register's value byte has been getting NACK'd. Raising
       to HIGH gives the input path the most margin available; dial
       back down only if there's a concrete reason to (e.g. EMI). */
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    /* GPIO_AF3_I2C2 -- verified against both the vendored device header
       (stm32u0xx_hal_gpio_ex.h: GPIO_AF3_I2C2 exists) and the STM32U031
       datasheet's Port A alternate-function table: PA6 lists I2C2_SDA
       at AF3, PA7 lists I2C2_SCL at AF3. No SYSCFG remap needed --
       unlike I2C1 on PA9/PA10, these pins reach I2C2 directly. */
    gpio_init.Alternate = GPIO_AF3_I2C2;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}

void platform_i2c_slave_init(uint8_t addr) {
    /* Unlike I2C1, I2C2 has NO independent kernel-clock-source select
       on this device -- checked directly against the vendored header:
       RCC_PERIPHCLK_I2C1 and RCC_PERIPHCLK_I2C3 both exist as
       HAL_RCCEx_PeriphCLKConfig() selector bits, but RCC_PERIPHCLK_I2C2
       does not (even though RCC_I2C2CLKSOURCE_HSI/etc. constants exist
       in the header for consistency, there's no way to actually apply
       them to I2C2 via the HAL's peripheral-clock API on this part).
       I2C2 is therefore permanently on PCLK1, which tracks
       SystemClock_Config()'s ~4 MHz MSI -- too slow for the 400 kHz
       Fast-mode I2C1 used, so the Timing value below targets
       Standard-mode (100 kHz) instead, derived for a 4 MHz kernel
       clock. No HSI enable/wait is needed here at all (unlike the old
       I2C1 path), since I2C2 never leaves PCLK1. */
    __HAL_RCC_I2C2_CLK_ENABLE();
    i2c_pins_init();

    s_i2c_handle.Instance = I2C2;
    /* ~50 kHz (half of Standard-mode) at a 4 MHz I2C kernel clock
       (PCLK1/MSI -- see this function's comment on why I2C2 can't use
       HSI like I2C1 did) -- PRESC=0, SCLDEL=2, SDADEL=0, SCLH=36,
       SCLL=42 gives t_I2CCLK=250ns, SCLH=9250ns, SCLL=10750ns,
       t_SCL=20000ns -> ~50 kHz.
       History: the NACK on VAULT_REG_COMMAND/VAULT_CMD_DONE's value
       byte was originally reproduced with an nRF52840-based host and
       traced to that chip's documented TWI errata #149 (the first
       clock pulse after the slave exits clock stretching can be too
       short or lost) -- switching to a different host (ESP32-C3 based)
       fixed it at this same ~50 kHz setting. But bumping that same
       ESP32-C3 host up to the ~108 kHz Standard-mode value
       (0x00101013) broke it again, so this genuinely is (also) a real
       timing-margin problem on top of the nRF52840-specific errata --
       not purely host-silicon-specific. ~50 kHz is the confirmed
       working speed; do not raise it without hardware retesting on
       whichever host is actually in use. */
    s_i2c_handle.Init.Timing = 0x0020242A;
    s_i2c_handle.Init.OwnAddress1 = (uint32_t)(addr << 1);
    s_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c_handle.Init.OwnAddress2 = 0;
    s_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&s_i2c_handle);

    /* Previously never configured -- the analog noise filter defaults
       ON with ~150ns filter time (docs/stm32u031_i2c_slave_analysis.md
       issue #7), which can attenuate edges and eat into setup/hold
       margin on the receive path. Safe to call any time after
       __HAL_RCC_I2C2_CLK_ENABLE() has run (already done earlier in
       this function). */
    HAL_I2CEx_ConfigAnalogFilter(&s_i2c_handle, I2C_ANALOGFILTER_DISABLE);

    HAL_NVIC_SetPriority(I2C2_3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C2_3_IRQn);

    HAL_I2C_EnableListen_IT(&s_i2c_handle);
}

void platform_i2c_slave_deinit(void) {
    HAL_NVIC_DisableIRQ(I2C2_3_IRQn);
    HAL_I2C_DisableListen_IT(&s_i2c_handle);
    HAL_I2C_DeInit(&s_i2c_handle);
    __HAL_RCC_I2C2_CLK_DISABLE();
}

void I2C2_3_IRQHandler(void) {
    /* STM32U031's I2C2_3_IRQn (IRQ 24) is a single combined line for
       I2C2 AND I2C3 (see stm32u031xx.h: "I2C2 / I2C3 global
       interrupt"), and within that, still a single combined line for
       both event and error interrupts, matching I2C1's own
       event+error combining. Since I2C3 is never initialized by this
       backend, this handler only ever needs to service I2C2, but both
       HAL sub-handlers must still be called or NACK/bus-error
       conditions raised while listen mode is enabled would never be
       handled. */
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
    /* Real hardware showed the COMMAND register's value byte (the only
       2-total-byte write in the protocol: pointer + exactly 1 value
       byte) getting NACK'd specifically, independent of bus speed --
       ruled out as a raw timing-margin issue by testing at both ~108
       kHz and ~50 kHz with the same result. Every register's re-arm
       call was uniformly using I2C_NEXT_FRAME (telling the peripheral
       "expect a later continuation call"), even for the byte that's
       actually the final one before STOP for registers with a known
       fixed length -- untested for exactly this shape until COMMAND,
       since every other register write is 3+ bytes total.
       vault_i2c_registers_next_write_byte_is_last() tracks whether the
       byte about to be armed is that known-final byte, so this can
       correctly tell the peripheral I2C_LAST_FRAME instead. */
    uint32_t frame = vault_i2c_registers_next_write_byte_is_last() ? I2C_LAST_FRAME : I2C_NEXT_FRAME;
    HAL_I2C_Slave_Seq_Receive_IT(hi2c, &s_rx_byte, 1, frame);
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    s_tx_byte = vault_i2c_registers_on_read_request();
    HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    vault_i2c_registers_on_stop();
    HAL_I2C_EnableListen_IT(hi2c);
}

/* DIAGNOSTIC: not previously overridden (the default weak HAL_I2C_ErrorCallback()
   does nothing), so there was no reliable breakpoint target for inspecting a NACK
   as it happens. s_last_i2c_error / s_last_i2c_isr capture HAL's own error flags
   and the peripheral's raw ISR register at the moment of the error, for a
   debugger to read directly -- set a breakpoint on the __NOP() line below and
   inspect both, plus I2C2->CR2, while investigating the real hardware NACK on
   the COMMAND register's value byte (see git history / HAL_I2C_SlaveRxCpltCallback()'s
   comment for the full context). Remove once the root cause is confirmed. */
static volatile uint32_t s_last_i2c_error;
static volatile uint32_t s_last_i2c_isr;

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    s_last_i2c_error = HAL_I2C_GetError(hi2c);
    s_last_i2c_isr = hi2c->Instance->ISR;
    __NOP(); /* <-- breakpoint here */
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
       clock only, everything else -- including I2C2 -- still running
       and able to service the interrupt this is waiting for). Plain
       __WFI() with SLEEPDEEP clear halts only the CPU core clock until
       any enabled interrupt fires. */
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
    __WFI();
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
