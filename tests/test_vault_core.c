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

    TEST_ASSERT(host_mock_call_count() >= 7);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_MAIN_RAIL_ENABLE, host_mock_call_at(0)->call);
    TEST_ASSERT_EQ_INT(1, host_mock_call_at(0)->arg);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_I2C_SLAVE_INIT, host_mock_call_at(1)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_I2C_SLAVE_DEINIT, host_mock_call_at(2)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_BUS_ISOLATE, host_mock_call_at(3)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_MAIN_RAIL_ENABLE, host_mock_call_at(4)->call);
    TEST_ASSERT_EQ_INT(0, host_mock_call_at(4)->arg);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_WAKEUP_TIMER_ARM, host_mock_call_at(5)->call);
    TEST_ASSERT_EQ_INT(HOST_MOCK_CALL_ENTER_LOW_POWER_SLEEP, host_mock_call_at(6)->call);
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
