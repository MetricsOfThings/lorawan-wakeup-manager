#include "vault/vault_core.h"
#include "vault/platform.h"

int main(void) {
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
