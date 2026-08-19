#include "vault/vault_i2c_registers.h"
#include "test_framework.h"
#include <string.h>

static void simulate_write_transaction(const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        vault_i2c_registers_on_write_byte(bytes[i]);
    }
    vault_i2c_registers_on_stop();
}

static void simulate_read(uint8_t reg_addr, uint8_t *out, size_t count) {
    vault_i2c_registers_on_write_byte(reg_addr);
    for (size_t i = 0; i < count; i++) {
        out[i] = vault_i2c_registers_on_read_request();
    }
    vault_i2c_registers_on_stop();
}

static void test_status_starts_invalid(void) {
    vault_test_reset_all();
    uint8_t status;
    simulate_read(VAULT_REG_STATUS, &status, 1);
    TEST_ASSERT_EQ_INT(0, status & VAULT_STATUS_CONTEXT_VALID_BIT);
}

static void test_protocol_version(void) {
    vault_test_reset_all();
    uint8_t version;
    simulate_read(VAULT_REG_PROTOCOL_VERSION, &version, 1);
    TEST_ASSERT_EQ_INT(VAULT_PROTOCOL_VERSION, version);
}

static void test_context_length_roundtrip_little_endian(void) {
    vault_test_reset_all();
    uint16_t value = 5u; /* within VAULT_CONTEXT_SIZE (8 in host tests) -- no clamping */
    uint8_t write_bytes[] = {
        VAULT_REG_CONTEXT_LENGTH,
        (uint8_t)(value & 0xFFu),
        (uint8_t)((value >> 8) & 0xFFu)
    };
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    uint8_t readback[2];
    simulate_read(VAULT_REG_CONTEXT_LENGTH, readback, 2);
    TEST_ASSERT_EQ_INT(write_bytes[1], readback[0]);
    TEST_ASSERT_EQ_INT(write_bytes[2], readback[1]);
}

static void test_context_length_above_255_is_representable_and_clamped(void) {
    /* This is the whole point of widening CONTEXT_LENGTH from 1 byte to
       2: a value above 255 -- impossible to encode in the old 1-byte
       field, even though VAULT_CONTEXT_SIZE can now exceed 255 (320 on
       real hardware targets) -- must be representable on the wire, and
       still correctly clamp to VAULT_CONTEXT_SIZE if it exceeds the
       buffer. */
    vault_test_reset_all();
    uint16_t value = 300u; /* > 255, and > VAULT_CONTEXT_SIZE (8 in host tests) */
    uint8_t write_bytes[] = {
        VAULT_REG_CONTEXT_LENGTH,
        (uint8_t)(value & 0xFFu),
        (uint8_t)((value >> 8) & 0xFFu)
    };
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    uint8_t readback[2];
    simulate_read(VAULT_REG_CONTEXT_LENGTH, readback, 2);
    uint16_t got = (uint16_t)readback[0] | ((uint16_t)readback[1] << 8);
    TEST_ASSERT_EQ_INT(VAULT_CONTEXT_SIZE, got);
}

static void test_context_length_partial_write_does_not_commit(void) {
    /* Same atomic-commit guarantee as WAKE_INTERVAL_SEC: a transaction
       that writes only the low byte and stops must not corrupt the
       previously-committed value. */
    vault_test_reset_all();
    uint8_t full[] = { VAULT_REG_CONTEXT_LENGTH, 3, 0 };
    simulate_write_transaction(full, sizeof(full));

    uint8_t partial[] = { VAULT_REG_CONTEXT_LENGTH, 0xAA };
    simulate_write_transaction(partial, sizeof(partial));

    uint8_t readback[2];
    simulate_read(VAULT_REG_CONTEXT_LENGTH, readback, 2);
    TEST_ASSERT_EQ_INT(3, readback[0]);
    TEST_ASSERT_EQ_INT(0, readback[1]);
}

static void test_context_data_roundtrip_and_valid_flag(void) {
    vault_test_reset_all();

    uint8_t write_bytes[1 + VAULT_CONTEXT_SIZE];
    write_bytes[0] = VAULT_REG_CONTEXT_DATA;
    for (unsigned i = 0; i < VAULT_CONTEXT_SIZE; i++) {
        write_bytes[1 + i] = (uint8_t)(0xA0 + i);
    }
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    uint8_t readback[VAULT_CONTEXT_SIZE];
    simulate_read(VAULT_REG_CONTEXT_DATA, readback, VAULT_CONTEXT_SIZE);
    for (unsigned i = 0; i < VAULT_CONTEXT_SIZE; i++) {
        TEST_ASSERT_EQ_INT((uint8_t)(0xA0 + i), readback[i]);
    }

    uint8_t status;
    simulate_read(VAULT_REG_STATUS, &status, 1);
    TEST_ASSERT_EQ_INT(VAULT_STATUS_CONTEXT_VALID_BIT, status & VAULT_STATUS_CONTEXT_VALID_BIT);
    TEST_ASSERT(vault_state_context_valid());
}

static void test_context_data_write_without_stop_does_not_set_valid(void) {
    vault_test_reset_all();

    /* Write REG_CONTEXT_DATA bytes but never call on_stop() -- simulates
       a master that dies mid-write. s_context_valid must stay false
       (CONTEXT_DATA is supposed to commit atomically, only on a
       completed transaction) rather than flipping true on the first
       byte written, which is what the pre-fix code did. */
    vault_i2c_registers_on_write_byte(VAULT_REG_CONTEXT_DATA);
    vault_i2c_registers_on_write_byte(0xAA);
    vault_i2c_registers_on_write_byte(0xBB);

    /* No vault_i2c_registers_on_stop() call here -- that's the point:
       the master went away mid-transaction and no STOP was ever seen. */
    TEST_ASSERT(!vault_state_context_valid());
}

static void test_context_data_write_beyond_size_is_clamped(void) {
    vault_test_reset_all();

    uint8_t write_bytes[1 + VAULT_CONTEXT_SIZE + 4];
    write_bytes[0] = VAULT_REG_CONTEXT_DATA;
    for (unsigned i = 0; i < VAULT_CONTEXT_SIZE + 4; i++) {
        write_bytes[1 + i] = 0x11;
    }
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    uint8_t version;
    simulate_read(VAULT_REG_PROTOCOL_VERSION, &version, 1);
    TEST_ASSERT_EQ_INT(VAULT_PROTOCOL_VERSION, version);
}

static void test_command_done_only_after_stop(void) {
    vault_test_reset_all();
    TEST_ASSERT(!vault_i2c_registers_done_requested());

    vault_i2c_registers_on_write_byte(VAULT_REG_COMMAND);
    vault_i2c_registers_on_write_byte(VAULT_CMD_DONE);
    TEST_ASSERT(!vault_i2c_registers_done_requested());

    vault_i2c_registers_on_stop();
    TEST_ASSERT(vault_i2c_registers_done_requested());
}

static void test_done_requested_resets_for_new_cycle(void) {
    vault_test_reset_all();
    uint8_t bytes[] = { VAULT_REG_COMMAND, VAULT_CMD_DONE };
    simulate_write_transaction(bytes, sizeof(bytes));
    TEST_ASSERT(vault_i2c_registers_done_requested());

    vault_i2c_registers_reset_for_cycle();
    TEST_ASSERT(!vault_i2c_registers_done_requested());
}

static void test_wake_interval_roundtrip_little_endian(void) {
    vault_test_reset_all();
    uint32_t value = 0x12345678u;
    uint8_t write_bytes[5];
    write_bytes[0] = VAULT_REG_WAKE_INTERVAL_SEC;
    write_bytes[1] = (uint8_t)(value & 0xFFu);
    write_bytes[2] = (uint8_t)((value >> 8) & 0xFFu);
    write_bytes[3] = (uint8_t)((value >> 16) & 0xFFu);
    write_bytes[4] = (uint8_t)((value >> 24) & 0xFFu);
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    TEST_ASSERT_EQ_INT((long)value, (long)vault_state_wake_interval_sec());

    uint8_t readback[4];
    simulate_read(VAULT_REG_WAKE_INTERVAL_SEC, readback, 4);
    TEST_ASSERT_EQ_INT(write_bytes[1], readback[0]);
    TEST_ASSERT_EQ_INT(write_bytes[2], readback[1]);
    TEST_ASSERT_EQ_INT(write_bytes[3], readback[2]);
    TEST_ASSERT_EQ_INT(write_bytes[4], readback[3]);
}

static void test_wake_interval_partial_write_does_not_commit(void) {
    vault_test_reset_all();
    vault_state_set_wake_interval_sec(60u);

    uint8_t write_bytes[] = { VAULT_REG_WAKE_INTERVAL_SEC, 0xAA, 0xBB };
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    TEST_ASSERT_EQ_INT(60, (long)vault_state_wake_interval_sec());
}

static void test_next_write_byte_is_last_false_before_pointer(void) {
    vault_test_reset_all();
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last());
}

static void test_next_write_byte_is_last_true_right_after_command_pointer(void) {
    vault_test_reset_all();
    vault_i2c_registers_on_write_byte(VAULT_REG_COMMAND);
    TEST_ASSERT(vault_i2c_registers_next_write_byte_is_last());
    vault_i2c_registers_on_stop();
}

static void test_next_write_byte_is_last_true_right_after_context_length_pointer(void) {
    vault_test_reset_all();
    vault_i2c_registers_on_write_byte(VAULT_REG_CONTEXT_LENGTH);
    /* L=2: only after the pointer AND the first value byte does the
       next (second, final) value byte become "last". */
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last());
    vault_i2c_registers_on_write_byte(0xAAu);
    TEST_ASSERT(vault_i2c_registers_next_write_byte_is_last());
    vault_i2c_registers_on_stop();
}

static void test_next_write_byte_is_last_sequence_for_wake_interval(void) {
    vault_test_reset_all();
    vault_i2c_registers_on_write_byte(VAULT_REG_WAKE_INTERVAL_SEC);
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last()); /* 0 of 4 */
    vault_i2c_registers_on_write_byte(0x01u);
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last()); /* 1 of 4 */
    vault_i2c_registers_on_write_byte(0x00u);
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last()); /* 2 of 4 */
    vault_i2c_registers_on_write_byte(0x00u);
    TEST_ASSERT(vault_i2c_registers_next_write_byte_is_last());  /* 3 of 4: next is last */
    vault_i2c_registers_on_write_byte(0x00u);
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last()); /* 4 of 4: complete, no "next" */
    vault_i2c_registers_on_stop();
}

static void test_next_write_byte_is_last_always_false_for_context_data(void) {
    /* CONTEXT_DATA's real length is host-controlled (terminated by
       STOP whenever the host chooses), so it must never claim the
       next byte is the last one, no matter how many bytes have
       already arrived. */
    vault_test_reset_all();
    vault_i2c_registers_on_write_byte(VAULT_REG_CONTEXT_DATA);
    TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last());
    for (int i = 0; i < 10; i++) {
        vault_i2c_registers_on_write_byte((uint8_t)i);
        TEST_ASSERT(!vault_i2c_registers_next_write_byte_is_last());
    }
    vault_i2c_registers_on_stop();
}

int main(void) {
    RUN_TEST(test_status_starts_invalid);
    RUN_TEST(test_protocol_version);
    RUN_TEST(test_context_length_roundtrip_little_endian);
    RUN_TEST(test_context_length_above_255_is_representable_and_clamped);
    RUN_TEST(test_context_length_partial_write_does_not_commit);
    RUN_TEST(test_context_data_roundtrip_and_valid_flag);
    RUN_TEST(test_context_data_write_without_stop_does_not_set_valid);
    RUN_TEST(test_context_data_write_beyond_size_is_clamped);
    RUN_TEST(test_command_done_only_after_stop);
    RUN_TEST(test_done_requested_resets_for_new_cycle);
    RUN_TEST(test_wake_interval_roundtrip_little_endian);
    RUN_TEST(test_wake_interval_partial_write_does_not_commit);
    RUN_TEST(test_next_write_byte_is_last_false_before_pointer);
    RUN_TEST(test_next_write_byte_is_last_true_right_after_command_pointer);
    RUN_TEST(test_next_write_byte_is_last_true_right_after_context_length_pointer);
    RUN_TEST(test_next_write_byte_is_last_sequence_for_wake_interval);
    RUN_TEST(test_next_write_byte_is_last_always_false_for_context_data);
    printf("%d/%d tests passed\n", g_test_count - g_test_failures, g_test_count);
    return g_test_failures != 0;
}
