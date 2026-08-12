#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "system_efm32g.h"
#include "vault/vault_log.h"

/* TX-only debug UART on PC0 (USART1) -- matching the TX-only pattern both
   other backends already use (bring-up logging need is one-way). Board
   schematic labels PC0/PC1 "US1_TX"/"US1_RX".

   USART1 ROUTE LOCATION verified against the real vendored alternate-
   function tables (same discipline Task 5 used for I2C0's LOCATION,
   where the brief's guess turned out wrong): unlike that case, this
   backend's part is EFM32G210F128, whose device directory is
   platform/Device/SiliconLabs/EFM32G (see CMakeLists.txt's Task 1
   comment), so the applicable tables are efm32g_af_pins.h /
   efm32g_af_ports.h, not a per-part file (this part has no such file --
   confirmed, only efm32g_af_pins.h/efm32g_af_ports.h exist for the
   EFM32G family). Confirmed there:
     AF_USART1_TX_PORT(0) == 2 (gpioPortC, per em_gpio.h's GPIO_Port_TypeDef:
       gpioPortA=0, gpioPortB=1, gpioPortC=2, gpioPortD=3)
     AF_USART1_TX_PIN(0)  == 0  -> PC0
     AF_USART1_RX_PORT(0) == 2, AF_USART1_RX_PIN(0) == 1 -> PC1
   i.e. LOCATION 0 is exactly PC0(TX)/PC1(RX) -- the brief's guessed
   LOCATION value of 0 is correct here (LOCATION 1 would instead map to
   PD0/PD1). This was verified, not assumed. */
#define USART1_TX_PORT gpioPortC
#define USART1_TX_PIN  0
#define USART1_ROUTE_LOCATION 0 /* Confirmed against efm32g_af_pins.h /
                                    efm32g_af_ports.h -- see comment above. */

void efm32g210_uart_init(void) {
    CMU_ClockEnable(cmuClock_USART1, true);

    /* GPIO peripheral clock is already enabled by efm32g210_gpio_init(),
       called earlier in platform_init() (see platform_efm32g210.c) --
       no separate CMU_ClockEnable(cmuClock_GPIO, ...) needed here. */
    GPIO_PinModeSet(USART1_TX_PORT, USART1_TX_PIN, gpioModePushPull, 1);

    /* Previously-flagged SystemCoreClock staleness risk (left open at
       Task 4): SystemCoreClock is still at this file's static-initializer
       reset default (14 MHz HFRCO) because nothing before this task ever
       called SystemCoreClockGet() to refresh it after efm32g210_clock_init()
       switched the main clock to HFXO (32 MHz).

       Checked before writing this baud-rate init (per this task's brief):
       does USART_InitAsync()'s baud divisor calculation actually read
       that stale `SystemCoreClock` global? Traced the real call chain in
       the vendored source (not assumed):
         USART_InitAsync(usart, init)               [em_usart.c]
           -> USART_BaudrateAsyncSet(usart, init->refFreq, ...)
              init->refFreq is left 0 by USART_INITASYNC_DEFAULT ("use
              current configured reference clock"), so:
              refFreq = CMU_ClockFreqGet(cmuClock_HFPER)   [em_cmu.c]
                -> (HFPER branch, no _CMU_HFPERPRESCB_MASK on this Series-0
                    part) ret = SystemHFClockGet()          [system_efm32g.c]
                     -> reads CMU->STATUS directly (HFXOSEL/HFRCOSEL/...)
                        and returns SystemHFXOClock (== EFM32_HFXO_FREQ ==
                        32000000UL) once HFXO is the live-selected source --
                        a fresh register read, NOT the static SystemCoreClock
                        variable.
       So for this exact call path, USART_InitAsync() does NOT depend on
       the stale SystemCoreClock global at all -- it independently
       recomputes the true HFCLK frequency from live CMU_STATUS bits every
       time, and by the time this runs (efm32g210_clock_init() has already
       switched HF to HFXO earlier in platform_init()), that recomputation
       correctly yields 32 MHz. Verified this is not merely theoretical by
       reading SystemHFClockGet()'s body directly (system_efm32g.c) rather
       than trusting the doc comment.

       SystemCoreClockGet() is still called here regardless, as a
       deliberate, cheap, zero-risk side effect: it refreshes the
       SystemCoreClock global itself (which SystemHFClockGet() does not
       touch) to the real ~32 MHz value, closing the gap Task 4 flagged
       for any *other* future reader of that global -- e.g. CMSIS
       SysTick_Config() or any other emlib/user code that reads
       SystemCoreClock directly instead of going through
       SystemHFClockGet()/CMU_ClockFreqGet(). This call is not required
       for this task's own UART baud-rate correctness (proven above), but
       is the "fix this some other way you can justify" option: it is a
       strict improvement with no downside, and this is the first point
       in the boot sequence where refreshing it becomes relevant. */
    (void)SystemCoreClockGet();

    USART_InitAsync_TypeDef init = USART_INITASYNC_DEFAULT;
    init.baudrate = 57600; /* matches the fixed baud rate both other
                               backends already settled on */
    USART_InitAsync(USART1, &init);

    USART1->ROUTE = USART_ROUTE_TXPEN | USART1_ROUTE_LOCATION;
}

/* core/'s vault_log() contract (vault/vault_log.h). Only called from
   vault_core's normal thread-mode control flow, never from the I2C0
   ISR -- matching both other backends' convention (see the comment in
   vault_core.c). */
void vault_log(const char *msg) {
    while (*msg != '\0') {
        USART_Tx(USART1, (uint8_t)*msg);
        msg++;
    }
}
