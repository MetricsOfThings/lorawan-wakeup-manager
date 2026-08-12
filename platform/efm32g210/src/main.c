#include "vault/platform.h"

extern void efm32g210_gpio_init(void);

/* platform_init() starts here with just GPIO init; Tasks 4 and 7 will
   extend it to also call clock/RTC init and, when VAULT_LOG_ENABLED,
   UART init -- matching the pattern platform_stm32u031.c's
   platform_init() already establishes. */
void platform_init(void) {
    efm32g210_gpio_init();
}

/* vault_core_init()/vault_core_step() are not wired in yet: vault_core
 * is not yet linked into this target (see the CMakeLists.txt comment on
 * vault_efm32g210) and no caller needs it until Task 6 ("EFM32G210
 * sleep entry (EM2) and full main() wiring"), matching the precedent
 * set by LPC810's Task 10 and STM32U031's Task 14, which are the tasks
 * that first made main() call vault_core_init()/vault_core_step(). Only
 * platform_init() is called here for now, exercising the GPIO init this
 * task adds without requiring vault_core to link.
 */
int main(void) {
    platform_init();

    while (1) { }
}
