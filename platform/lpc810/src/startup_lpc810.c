#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void WKT_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C0_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0, /* reserved */
    SVC_Handler,
    0, 0, /* reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* IRQ0-31, per LPC8xx.h's IRQn_Type enum (confirmed in Task 8 against
       the vendored header). Only WKT_IRQn (=15) is wired up so far;
       I2C0_IRQn (=8) is filled in here too since its position is already
       known, but Task 9 owns actually implementing/verifying it -- for
       now it points at Default_Handler like the rest of the unused slots.
       Array covers the full IRQ0-31 range per device specification
       (NUMBER_OF_INT_VECTORS = 48: 16 Cortex-M0+ exceptions + 32 device IRQs). */
    0,                  /* IRQ0:  SPI0_IRQn */
    0,                  /* IRQ1:  SPI1_IRQn */
    0,                  /* IRQ2:  Reserved18_IRQn */
    0,                  /* IRQ3:  USART0_IRQn */
    0,                  /* IRQ4:  USART1_IRQn */
    0,                  /* IRQ5:  USART2_IRQn */
    0,                  /* IRQ6:  Reserved22_IRQn */
    0,                  /* IRQ7:  Reserved23_IRQn */
    I2C0_IRQHandler,    /* IRQ8:  I2C0_IRQn (Task 9 implements) */
    0,                  /* IRQ9:  SCT0_IRQn */
    0,                  /* IRQ10: MRT0_IRQn */
    0,                  /* IRQ11: CMP_IRQn */
    0,                  /* IRQ12: WDT_IRQn */
    0,                  /* IRQ13: BOD_IRQn */
    0,                  /* IRQ14: Reserved30_IRQn */
    WKT_IRQHandler,     /* IRQ15: WKT_IRQn */
    0,                  /* IRQ16: Reserved32_IRQn */
    0,                  /* IRQ17: Reserved33_IRQn */
    0,                  /* IRQ18: Reserved34_IRQn */
    0,                  /* IRQ19: Reserved35_IRQn */
    0,                  /* IRQ20: Reserved36_IRQn */
    0,                  /* IRQ21: Reserved37_IRQn */
    0,                  /* IRQ22: Reserved38_IRQn */
    0,                  /* IRQ23: Reserved39_IRQn */
    0,                  /* IRQ24: PIN_INT0_IRQn */
    0,                  /* IRQ25: PIN_INT1_IRQn */
    0,                  /* IRQ26: PIN_INT2_IRQn */
    0,                  /* IRQ27: PIN_INT3_IRQn */
    0,                  /* IRQ28: PIN_INT4_IRQn */
    0,                  /* IRQ29: PIN_INT5_IRQn */
    0,                  /* IRQ30: PIN_INT6_IRQn */
    0,                  /* IRQ31: PIN_INT7_IRQn */
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
