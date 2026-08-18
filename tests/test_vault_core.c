#include "vault/vault_core.h"
#include "vault/vault_i2c_registers.h"
#include "vault/host_mock_test_api.h"
#include "test_framework.h"

static void queue_done_command(void) {
    uint8_t bytes[] = { VAULT_REG_COMMAND, VAULT_CMD_DONE };
    host_mock_queue_write_transaction(bytes, sizeof(bytes));
}

static const host_mock_call_record_t *find_call(host_mock_call_t call) {
    for (size_t i = 0; i < host_mock_call_count(); i++) {
        if (host_mock_call_at(i)->call == call) {
            return host_mock_call_at(i);
        }
    }
    return NULL;
}

static void test_first_cycle_calls_in_order(void) {
    host_mock_reset();
    vault_test_reset_all();
    vault_core_init();
    queue_done_command();

    vault_core_step();

    /* vault_core.c's WAKE_MAIN wait loop is now:
           for (;;) {
               platform_irq_disable();
               if (vault_i2c_registers_done_requested()) {
                   platform_irq_enable();
                   break;
               }
               platform_wait_for_interrupt();
               platform_irq_enable();
           }
       (see Critical #1's fix -- masking interrupts around the check
       closes a lost-wakeup race). The host mock only ever replays queued
       transactions from inside platform_wait_for_interrupt(), so
       done_requested() is false on the loop's first entry: iteration 1
       disables, checks (false), waits (which replays the queued CMD_DONE
       write and makes done_requested() true), then re-enables.
       Iteration 2 disables, checks (now true), re-enables, and breaks --
       without ever calling platform_wait_for_interrupt() again. That's
       IRQ_DISABLE, WAIT_FOR_INTERRUPT, IRQ_ENABLE, IRQ_DISABLE, IRQ_ENABLE,
       for a total of 5 calls covering the loop, 12 calls overall. */
    /* I2C1 must be live before the main MCU's rail turns on -- otherwise
       the main MCU can start polling for the version register before
       platform_i2c_slave_init() has run, finding SDA/SCL still in their
       unconfigured/floating reset state and blocking indefinitely
       before even attempting a START condition (observed on real
       STM32U031F8P6 hardware; see git history). */
    TEST_ASSERT(host_mock_call_count() >= 12);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_I2C_SLAVE_INIT, host_mock_call_at(0)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_MAIN_RAIL_ENABLE, host_mock_call_at(1)->call);
    TEST_ASSERT_EQ_INT(1, host_mock_call_at(1)->arg);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_IRQ_DISABLE, host_mock_call_at(2)->call);
    /* The mock replays queued I2C transactions from
       platform_wait_for_interrupt() (the wait loop's per-iteration
       hook), not from platform_i2c_slave_init() -- matching real
       hardware's ISR-driven timing, where nothing arrives until the
       loop actually waits. */
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_WAIT_FOR_INTERRUPT, host_mock_call_at(3)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_IRQ_ENABLE, host_mock_call_at(4)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_IRQ_DISABLE, host_mock_call_at(5)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_IRQ_ENABLE, host_mock_call_at(6)->call);
    /* The main rail must be cut BEFORE the I2C peripheral is torn down
       and the bus pins are reconfigured -- otherwise the master MCU is
       still powered and listening while those steps produce real
       transitions on SDA/SCL, right after it wrote CMD_DONE expecting
       an immediate, clean power loss. */
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_MAIN_RAIL_ENABLE, host_mock_call_at(7)->call);
    TEST_ASSERT_EQ_INT(0, host_mock_call_at(7)->arg);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_I2C_SLAVE_DEINIT, host_mock_call_at(8)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_BUS_ISOLATE, host_mock_call_at(9)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_WAKEUP_TIMER_ARM, host_mock_call_at(10)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_ENTER_LOW_POWER_SLEEP, host_mock_call_at(11)->call);
}

static void test_default_interval_used_on_first_cycle(void) {
    host_mock_reset();
    vault_test_reset_all();
    vault_core_init();
    queue_done_command();

    vault_core_step();

    const host_mock_call_record_t *arm_call = find_call(HOST_MOCK_CALL_WAKEUP_TIMER_ARM);
    TEST_ASSERT(arm_call != NULL);
    TEST_ASSERT_EQ_INT(VAULT_DEFAULT_WAKE_INTERVAL_SEC, arm_call->arg);
}

static void test_main_mcu_configured_interval_used_next_cycle(void) {
    host_mock_reset();
    vault_test_reset_all();
    vault_core_init();

    uint8_t interval_bytes[] = { VAULT_REG_WAKE_INTERVAL_SEC, 44, 1, 0, 0 }; /* 300 sec, LE */
    host_mock_queue_write_transaction(interval_bytes, sizeof(interval_bytes));
    queue_done_command();
    vault_core_step();

    host_mock_reset(); /* clear call log only; vault_core's own state persists */
    queue_done_command();
    vault_core_step();

    const host_mock_call_record_t *arm_call = find_call(HOST_MOCK_CALL_WAKEUP_TIMER_ARM);
    TEST_ASSERT(arm_call != NULL);
    TEST_ASSERT_EQ_INT(300, arm_call->arg);
}

int main(void) {
    RUN_TEST(test_first_cycle_calls_in_order);
    RUN_TEST(test_default_interval_used_on_first_cycle);
    RUN_TEST(test_main_mcu_configured_interval_used_next_cycle);
    printf("%d/%d tests passed\n", g_test_count - g_test_failures, g_test_count);
    return g_test_failures != 0;
}
