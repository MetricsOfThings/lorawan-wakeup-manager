#ifndef VAULT_HOST_MOCK_TEST_API_H
#define VAULT_HOST_MOCK_TEST_API_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HOST_MOCK_CALL_MAIN_RAIL_ENABLE,
    HOST_MOCK_CALL_I2C_SLAVE_INIT,
    HOST_MOCK_CALL_WAIT_FOR_INTERRUPT,
    HOST_MOCK_CALL_I2C_SLAVE_DEINIT,
    HOST_MOCK_CALL_BUS_ISOLATE,
    HOST_MOCK_CALL_WAKEUP_TIMER_ARM,
    HOST_MOCK_CALL_WAKEUP_TIMER_CLEAR,
    HOST_MOCK_CALL_ENTER_LOW_POWER_SLEEP
} host_mock_call_t;

typedef struct {
    host_mock_call_t call;
    uint32_t arg;
} host_mock_call_record_t;

/* Clears the call log and any queued (not-yet-replayed) transactions. */
void host_mock_reset(void);

size_t host_mock_call_count(void);
const host_mock_call_record_t *host_mock_call_at(size_t index);

/* Queues one simulated I2C write transaction (address byte + data bytes),
   replayed into vault_i2c_registers_on_write_byte()/on_stop() the next
   time platform_wait_for_interrupt() is called (the mock's stand-in for
   "an I2C interrupt occurred") -- not at platform_i2c_slave_init() time,
   so vault_core's wait loop genuinely needs at least one iteration to
   observe queued transactions, matching real hardware's ISR-driven
   timing. Call multiple times to queue several transactions, replayed in
   order. If a test wants vault_core's wait loop to exit, at least one
   queued transaction must write VAULT_CMD_DONE to VAULT_REG_COMMAND --
   this mock is fully synchronous, so nothing else will ever set
   done_requested after wait_for_interrupt() returns. */
void host_mock_queue_write_transaction(const uint8_t *bytes, size_t count);

#endif /* VAULT_HOST_MOCK_TEST_API_H */
