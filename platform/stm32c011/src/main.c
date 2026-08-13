#include "vault/vault_core.h"
#include "vault/platform.h"

/* Unlike platform/stm32u031/src/main.c, no SystemClock_Config() call is
 * needed here: Task 4 puts STM32C011's clock configuration entirely
 * inside platform_init(). HSI is the reset-default clock source on this
 * family (unlike STM32U031's MSI-based tree), so there is no separate
 * clock-tree setup that must run before platform_init()'s own HAL
 * calls. Confirmed in Task 4 rather than assumed here. */
int main(void) {
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
