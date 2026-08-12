#include "vault/platform.h"

/* platform_init() (GPIO + clock/RTC init) is now implemented in
   platform_efm32g210.c (Task 4), which supersedes the placeholder that
   used to live here calling only efm32g210_gpio_init(). Task 7 will
   extend platform_init() further to add UART init when
   VAULT_LOG_ENABLED, matching the pattern platform_stm32u031.c's
   platform_init() already establishes. */

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
