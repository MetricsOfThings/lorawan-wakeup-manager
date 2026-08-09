#include "vault/vault_log.h"

#ifdef VAULT_LOG_ENABLED

void vault_log_u32(const char *label, uint32_t value) {
    /* Manual decimal conversion instead of snprintf("%lu"), to avoid
       pulling libc's formatted-output code into the LPC810's 4 KB
       flash budget. */
    char digits[10]; /* max uint32_t is 10 decimal digits */
    int count = 0;

    if (value == 0) {
        digits[count++] = '0';
    } else {
        while (value > 0) {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    char buf[64];
    int pos = 0;
    while (label[pos] != '\0' && pos < (int)sizeof(buf) - 1) {
        buf[pos] = label[pos];
        pos++;
    }
    while (count > 0 && pos < (int)sizeof(buf) - 2) {
        buf[pos++] = digits[--count];
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';

    vault_log(buf);
}

#endif /* VAULT_LOG_ENABLED */
