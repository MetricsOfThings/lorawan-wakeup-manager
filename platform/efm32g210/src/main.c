#include "vault/vault_core.h"
#include "vault/platform.h"

/* platform_init() (GPIO + clock/RTC init) is implemented in
   platform_efm32g210.c (Task 4), which supersedes the placeholder that
   used to live here calling only efm32g210_gpio_init(). Task 7 will
   extend platform_init() further to add UART init when
   VAULT_LOG_ENABLED, matching the pattern platform_stm32u031.c's
   platform_init() already establishes.

   vault_core_init()/vault_core_step() are wired in as of this task
   (Task 6, "EFM32G210 sleep entry (EM2) and full main() wiring"), now
   that platform_enter_low_power_sleep() (platform_efm32g210.c) gives
   vault_core_step() a real EM2 sleep to enter -- the same point (once
   every platform.h contract function has a real implementation) both
   other backends first made main() call vault_core_init()/
   vault_core_step() (LPC810's Task 10, STM32U031's Task 14). */
int main(void) {
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
