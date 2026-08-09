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
       Extend this array further (do not shrink or renumber it) for any
       IRQn beyond 15 that a later task needs. */
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
