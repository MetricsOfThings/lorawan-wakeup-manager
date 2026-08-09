#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
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

    /* Select the RTC clock source. LSI is used as a placeholder since no
       board exists yet to confirm whether an LSE crystal is fitted --
       the same "no board to confirm against" reasoning SystemClock_Config()
       already applies to MSI vs the rest of the clock tree. Revisit once
       hardware exists: LSE (typically 32.768 kHz) is normally preferred
       for RTC timekeeping accuracy over LSI's much looser tolerance. */
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
    /* HAL_RTCEx_SetWakeUpTimer's tick source and the arithmetic to turn
       `seconds` into a wakeup-counter reload value depend on the RTC
       clock source (LSE vs LSI) chosen for the real board -- this uses
       the RTCCLK/16 wakeup clock (WUCKSEL_RTCCLK_DIV16), which needs the
       RTC clock frequency confirmed against the STM32U031 reference
       manual's RTC chapter before the `seconds`-to-ticks arithmetic
       below can be trusted. Placeholder: assumes a 1 Hz effective tick,
       i.e. reload value == seconds, which is only true for specific
       clock configurations -- verify before flashing once hardware
       exists. */
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

void platform_i2c_slave_init(uint8_t addr) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    i2c_pins_init();

    s_i2c_handle.Instance = I2C1;
    s_i2c_handle.Init.Timing = 0x00303D5B; /* 100 kHz at an assumed 16 MHz
                                               I2C clock -- verify against
                                               CubeMX's timing calculator
                                               for the real clock config
                                               before flashing */
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
