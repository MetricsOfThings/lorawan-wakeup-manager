#include "vault/vault_i2c_registers.h"

typedef enum {
    REG_NONE = -1,
    REG_STATUS = VAULT_REG_STATUS,
    REG_PROTOCOL_VERSION = VAULT_REG_PROTOCOL_VERSION,
    REG_CONTEXT_LENGTH = VAULT_REG_CONTEXT_LENGTH,
    REG_CONTEXT_DATA = VAULT_REG_CONTEXT_DATA,
    REG_COMMAND = VAULT_REG_COMMAND,
    REG_WAKE_INTERVAL_SEC = VAULT_REG_WAKE_INTERVAL_SEC
} vault_reg_t;

static uint8_t  s_context[VAULT_CONTEXT_SIZE];
static uint8_t  s_context_length;
static bool     s_context_valid;
static uint32_t s_wake_interval_sec;
static uint32_t s_wake_interval_staging;

static int      s_active_reg = REG_NONE;
static uint16_t s_field_offset;
static bool     s_have_pointer;
static bool     s_pending_done;
static bool     s_done_requested;

void vault_i2c_registers_on_write_byte(uint8_t byte) {
    if (!s_have_pointer) {
        s_have_pointer = true;
        s_field_offset = 0;
        switch (byte) {
        case REG_STATUS:
        case REG_PROTOCOL_VERSION:
        case REG_CONTEXT_LENGTH:
        case REG_CONTEXT_DATA:
        case REG_COMMAND:
        case REG_WAKE_INTERVAL_SEC:
            s_active_reg = (vault_reg_t)byte;
            break;
        default:
            s_active_reg = REG_NONE;
            break;
        }
        return;
    }

    switch (s_active_reg) {
    case REG_CONTEXT_LENGTH:
        if (s_field_offset == 0) {
            s_context_length = (byte > VAULT_CONTEXT_SIZE) ? (uint8_t)VAULT_CONTEXT_SIZE : byte;
        }
        break;
    case REG_CONTEXT_DATA:
        if (s_field_offset < VAULT_CONTEXT_SIZE) {
            s_context[s_field_offset] = byte;
            s_context_valid = true;
        }
        break;
    case REG_COMMAND:
        if (s_field_offset == 0 && byte == VAULT_CMD_DONE) {
            s_pending_done = true;
        }
        break;
    case REG_WAKE_INTERVAL_SEC:
        if (s_field_offset < 4) {
            s_wake_interval_staging &= ~(0xFFu << (8u * s_field_offset));
            s_wake_interval_staging |= ((uint32_t)byte) << (8u * s_field_offset);
            if (s_field_offset == 3) {
                s_wake_interval_sec = s_wake_interval_staging;
            }
        }
        break;
    case REG_STATUS:
    case REG_PROTOCOL_VERSION:
    default:
        break; /* read-only or unknown register: writes ignored */
    }
    s_field_offset++;
}

uint8_t vault_i2c_registers_on_read_request(void) {
    uint8_t value = 0xFFu;

    if (!s_have_pointer) {
        return value;
    }

    switch (s_active_reg) {
    case REG_STATUS:
        if (s_field_offset == 0) {
            value = s_context_valid ? VAULT_STATUS_CONTEXT_VALID_BIT : 0x00u;
        }
        break;
    case REG_PROTOCOL_VERSION:
        if (s_field_offset == 0) {
            value = VAULT_PROTOCOL_VERSION;
        }
        break;
    case REG_CONTEXT_LENGTH:
        if (s_field_offset == 0) {
            value = s_context_length;
        }
        break;
    case REG_CONTEXT_DATA:
        if (s_field_offset < VAULT_CONTEXT_SIZE) {
            value = s_context[s_field_offset];
        }
        break;
    case REG_WAKE_INTERVAL_SEC:
        if (s_field_offset < 4) {
            value = (uint8_t)(s_wake_interval_sec >> (8u * s_field_offset));
        }
        break;
    case REG_COMMAND:
    default:
        break;
    }

    s_field_offset++;
    return value;
}

void vault_i2c_registers_on_stop(void) {
    if (s_pending_done) {
        s_done_requested = true;
        s_pending_done = false;
    }
    s_have_pointer = false;
    s_field_offset = 0;
    s_active_reg = REG_NONE;
}

void vault_i2c_registers_reset_for_cycle(void) {
    s_done_requested = false;
    s_pending_done = false;
    s_have_pointer = false;
    s_field_offset = 0;
    s_active_reg = REG_NONE;
}

bool vault_i2c_registers_done_requested(void) {
    return s_done_requested;
}

bool vault_state_context_valid(void) {
    return s_context_valid;
}

uint32_t vault_state_wake_interval_sec(void) {
    return s_wake_interval_sec;
}

void vault_state_set_wake_interval_sec(uint32_t seconds) {
    s_wake_interval_sec = seconds;
}

void vault_test_reset_all(void) {
    for (unsigned i = 0; i < VAULT_CONTEXT_SIZE; i++) {
        s_context[i] = 0;
    }
    s_context_length = 0;
    s_context_valid = false;
    s_wake_interval_sec = 0;
    s_wake_interval_staging = 0;
    vault_i2c_registers_reset_for_cycle();
}
