#include <stdint.h>

#include "LPC8xx.h"

/* Referencing these pointers (without dereferencing) is enough to prove
   the vendored header defines the peripheral base addresses this
   project needs later, and that it parses cleanly under
   arm-none-eabi-gcc -mcpu=cortex-m0plus.

   NOTE: The vendored header (NXP/Keil.LPC800_DFP pack, current release
   1.10.2) does not use the LPC_-prefixed macro names from the original
   task brief. That umbrella "LPC8xx.h" is now an obsolete #error stub in
   the current pack; the real per-device header is "LPC810.h", which uses
   unprefixed peripheral names. The actual macro names are:
     LPC_SYSCON    -> SYSCON
     LPC_GPIO_PORT -> GPIO
     LPC_SWM       -> SWM0
     LPC_WKT       -> WKT
     LPC_I2C0      -> I2C0
   See platform/lpc810/vendor/LPC8xx.h (copied verbatim from LPC810.h) for
   the authoritative definitions. Later tasks (7-9) must use these names. */
static void *const s_peripheral_check[] = {
    (void *)SYSCON,
    (void *)GPIO,
    (void *)SWM0,
    (void *)WKT,
    (void *)I2C0,
};

int lpc810_cmsis_check_reference(void) {
    return (int)(intptr_t)s_peripheral_check[0];
}
