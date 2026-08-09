#include <stdio.h>

#include "vault/vault_log.h"

/* core/'s vault_log() contract, for host-side test runs (e.g. `ctest
   -V`) when VAULT_LOG_ENABLED is on. Not needed by the test suite
   itself -- only useful for a human watching test output while
   debugging vault_core's logic locally. */
void vault_log(const char *msg) {
    fputs(msg, stderr);
}
