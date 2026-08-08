#ifndef VAULT_I2C_REGISTERS_H
#define VAULT_I2C_REGISTERS_H

#include <stdint.h>
#include <stdbool.h>

#define VAULT_REG_STATUS            0x00u
#define VAULT_REG_PROTOCOL_VERSION  0x01u
#define VAULT_REG_CONTEXT_LENGTH    0x02u
#define VAULT_REG_CONTEXT_DATA      0x03u
#define VAULT_REG_COMMAND           0x04u
#define VAULT_REG_WAKE_INTERVAL_SEC 0x05u

#define VAULT_CMD_DONE              0x01u
#define VAULT_PROTOCOL_VERSION      0x01u
#define VAULT_STATUS_CONTEXT_VALID_BIT (1u << 0)

#ifndef VAULT_CONTEXT_SIZE
#define VAULT_CONTEXT_SIZE 128u
#endif

/* I2C wire-protocol hooks. A backend's I2C slave interrupt handler calls
   these as raw bytes arrive/are requested/the bus goes idle. See
   vault/platform.h and the design spec section 5 for the framing rules. */
void    vault_i2c_registers_on_write_byte(uint8_t byte);
uint8_t vault_i2c_registers_on_read_request(void);
void    vault_i2c_registers_on_stop(void);

/* Called by vault_core at the start of each wake cycle: clears the
   in-progress-transaction pointer state and the "done" latch, without
   touching context_valid / context data / wake_interval_sec. */
void vault_i2c_registers_reset_for_cycle(void);

/* True once the main MCU has written COMMAND=VAULT_CMD_DONE and the
   transaction's STOP condition has been seen. */
bool vault_i2c_registers_done_requested(void);

/* Direct C accessors into the same storage the register handlers above
   read and write (not part of the wire protocol). */
bool     vault_state_context_valid(void);
uint32_t vault_state_wake_interval_sec(void);
void     vault_state_set_wake_interval_sec(uint32_t seconds);

/* Full state reset for test isolation. Not used by production code —
   real hardware relies on cold-boot zero-initialization instead. */
void vault_test_reset_all(void);

#endif /* VAULT_I2C_REGISTERS_H */
