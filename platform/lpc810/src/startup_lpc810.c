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
    /* IRQ0-31 -- only the two this project uses are named; the rest
       fall through to Default_Handler via the weak aliases above once
       CMSIS's actual IRQn_Type enum order is confirmed in Task 8/9
       against LPC8xx.h (the position of WKT_IRQn and I2C0_IRQn in that
       enum must match their position in this table -- verify before
       relying on either interrupt firing). */
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
