#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Cortex-M3 system exceptions. */
void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));

/* EFM32G210F128 device interrupts that this backend actually uses (later
   tasks override these by strong symbol): RTC (Task 4, wake timer) and
   I2C0 (Task 5, slave driver). */
void RTC_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C0_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,         /* reserved */
    SVC_Handler,
    DebugMon_Handler,
    0,                  /* reserved (sl_app_properties slot in vendor SDK; unused here) */
    PendSV_Handler,
    SysTick_Handler,

    /* External interrupts. Position/order taken verbatim from the
       vendored platform/Device/SiliconLabs/EFM32G/Source/GCC/startup_efm32g.S
       (the authoritative Silicon Labs GCC startup file for the whole
       EFM32G family), cross-checked against this exact part's IRQn_Type
       enum in platform/Device/SiliconLabs/EFM32G/Include/efm32g210f128.h.
       efm32g210f128.h's IRQn_Type omits several slots present in the
       family-wide startup file (TIMER2, USART2_RX/TX, UART0_RX/TX,
       LEUART1, PCNT1, PCNT2, LCD -- not present on this smaller G210
       part); those positions are kept as reserved/0 below, matching the
       gaps confirmed in efm32g210f128.h. */
    0,                  /* IRQ0:  DMA_IRQn */
    0,                  /* IRQ1:  GPIO_EVEN_IRQn */
    0,                  /* IRQ2:  TIMER0_IRQn */
    0,                  /* IRQ3:  USART0_RX_IRQn */
    0,                  /* IRQ4:  USART0_TX_IRQn */
    0,                  /* IRQ5:  ACMP0_IRQn */
    0,                  /* IRQ6:  ADC0_IRQn */
    0,                  /* IRQ7:  DAC0_IRQn */
    I2C0_IRQHandler,    /* IRQ8:  I2C0_IRQn */
    0,                  /* IRQ9:  GPIO_ODD_IRQn */
    0,                  /* IRQ10: TIMER1_IRQn */
    0,                  /* IRQ11: reserved on G210 (TIMER2_IRQn on other EFM32G parts) */
    0,                  /* IRQ12: USART1_RX_IRQn */
    0,                  /* IRQ13: USART1_TX_IRQn */
    0,                  /* IRQ14: reserved on G210 (USART2_RX_IRQn on other EFM32G parts) */
    0,                  /* IRQ15: reserved on G210 (USART2_TX_IRQn on other EFM32G parts) */
    0,                  /* IRQ16: reserved on G210 (UART0_RX_IRQn on other EFM32G parts) */
    0,                  /* IRQ17: reserved on G210 (UART0_TX_IRQn on other EFM32G parts) */
    0,                  /* IRQ18: LEUART0_IRQn */
    0,                  /* IRQ19: reserved on G210 (LEUART1_IRQn on other EFM32G parts) */
    0,                  /* IRQ20: LETIMER0_IRQn */
    0,                  /* IRQ21: PCNT0_IRQn */
    0,                  /* IRQ22: reserved on G210 (PCNT1_IRQn on other EFM32G parts) */
    0,                  /* IRQ23: reserved on G210 (PCNT2_IRQn on other EFM32G parts) */
    RTC_IRQHandler,     /* IRQ24: RTC_IRQn */
    0,                  /* IRQ25: CMU_IRQn */
    0,                  /* IRQ26: VCMP_IRQn */
    0,                  /* IRQ27: reserved on G210 (LCD_IRQn on other EFM32G parts) */
    0,                  /* IRQ28: MSC_IRQn */
    0,                  /* IRQ29: AES_IRQn */
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }
    main();
    while (1) { }
}

void Default_Handler(void) {
    while (1) { }
}
