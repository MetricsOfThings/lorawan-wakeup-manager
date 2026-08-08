#include "vault/platform.h"
#include "vault/host_mock_test_api.h"
#include "vault/vault_i2c_registers.h"
#include <string.h>

#define HOST_MOCK_MAX_CALLS 32
#define HOST_MOCK_MAX_QUEUED_TRANSACTIONS 8
#define HOST_MOCK_MAX_TRANSACTION_BYTES 16

static host_mock_call_record_t s_calls[HOST_MOCK_MAX_CALLS];
static size_t s_call_count;

typedef struct {
    uint8_t bytes[HOST_MOCK_MAX_TRANSACTION_BYTES];
    size_t length;
} host_mock_transaction_t;

static host_mock_transaction_t s_queued[HOST_MOCK_MAX_QUEUED_TRANSACTIONS];
static size_t s_queued_count;

static void record_call(host_mock_call_t call, uint32_t arg) {
    if (s_call_count < HOST_MOCK_MAX_CALLS) {
        s_calls[s_call_count].call = call;
        s_calls[s_call_count].arg = arg;
        s_call_count++;
    }
}

void host_mock_reset(void) {
    s_call_count = 0;
    s_queued_count = 0;
    memset(s_calls, 0, sizeof(s_calls));
    memset(s_queued, 0, sizeof(s_queued));
}

size_t host_mock_call_count(void) {
    return s_call_count;
}

const host_mock_call_record_t *host_mock_call_at(size_t index) {
    return &s_calls[index];
}

void host_mock_queue_write_transaction(const uint8_t *bytes, size_t count) {
    if (s_queued_count >= HOST_MOCK_MAX_QUEUED_TRANSACTIONS) {
        return;
    }
    if (count > HOST_MOCK_MAX_TRANSACTION_BYTES) {
        count = HOST_MOCK_MAX_TRANSACTION_BYTES;
    }
    memcpy(s_queued[s_queued_count].bytes, bytes, count);
    s_queued[s_queued_count].length = count;
    s_queued_count++;
}

void platform_init(void) {
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    record_call(HOST_MOCK_CALL_WAKEUP_TIMER_ARM, seconds);
}

void platform_wakeup_timer_clear(void) {
    record_call(HOST_MOCK_CALL_WAKEUP_TIMER_CLEAR, 0);
}

void platform_main_rail_enable(bool on) {
    record_call(HOST_MOCK_CALL_MAIN_RAIL_ENABLE, on ? 1u : 0u);
}

void platform_i2c_slave_init(uint8_t addr) {
    record_call(HOST_MOCK_CALL_I2C_SLAVE_INIT, addr);
    for (size_t t = 0; t < s_queued_count; t++) {
        for (size_t i = 0; i < s_queued[t].length; i++) {
            vault_i2c_registers_on_write_byte(s_queued[t].bytes[i]);
        }
        vault_i2c_registers_on_stop();
    }
    s_queued_count = 0;
}

void platform_i2c_slave_deinit(void) {
    record_call(HOST_MOCK_CALL_I2C_SLAVE_DEINIT, 0);
}

void platform_bus_isolate(void) {
    record_call(HOST_MOCK_CALL_BUS_ISOLATE, 0);
}

void platform_enter_low_power_sleep(void) {
    record_call(HOST_MOCK_CALL_ENTER_LOW_POWER_SLEEP, 0);
}
