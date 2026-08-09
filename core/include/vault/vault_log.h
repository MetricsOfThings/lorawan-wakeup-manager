#ifndef VAULT_LOG_H
#define VAULT_LOG_H

#include <stdint.h>

/* Application-level debug logging for vault_core's state machine.
   Platform-agnostic by design (like platform.h): core/ calls only
   these two functions, and each backend provides one implementation
   of vault_log() when VAULT_LOG_ENABLED is on (see the VAULT_LOG_ENABLED
   CMake option). When it's off, both calls compile to nothing -- no
   backend implementation is required, and call sites in core/ never
   need `#ifdef` guards. */

#ifdef VAULT_LOG_ENABLED

/* Implemented once per backend. Must not be called from interrupt
   context on real hardware -- it typically blocks on a UART transmit,
   which is slow enough to corrupt I2C timing if called from the I2C
   ISR. Only ever call this from vault_core's normal thread-mode
   control flow. */
void vault_log(const char *msg);

/* Formats "<label><value>\n" using a small internal decimal
   converter (no libc printf/snprintf, to avoid its code-size cost on
   the 4 KB LPC810 build) and passes the result to vault_log(). */
void vault_log_u32(const char *label, uint32_t value);

#else

#define vault_log(msg) ((void)0)
#define vault_log_u32(label, value) ((void)0)

#endif /* VAULT_LOG_ENABLED */

#endif /* VAULT_LOG_H */
