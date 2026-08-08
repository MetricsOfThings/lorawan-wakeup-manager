# LoRaWAN Wakeup Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Data Vault firmware — a platform-agnostic core plus two real MCU backends (LPC810, STM32U031F8P6) — implementing the I2C register-map protocol and wake-scheduling state machine from the design spec.

**Architecture:** Three CMake-selected layers: `core/` (platform-agnostic state machine + I2C register map, unit-tested on the host), `platform.h` (the HAL contract), and one backend per target (`platform/lpc810`, `platform/stm32u031`, `platform/host_mock`). Only the backend differs per target; `core/` is byte-identical across all three.

**Tech Stack:** C11, CMake 3.20+, `arm-none-eabi-gcc` for the two hardware targets, native `gcc`/`clang` for the host target, bare CMSIS + NXP LPC81x device header for LPC810, STM32Cube HAL/LL for STM32U031F8P6.

## Global Constraints

- `VAULT_CONTEXT_SIZE`: 8 bytes (host tests), 64 bytes (LPC810), 128 bytes (STM32U031F8P6) — exact values from spec §4.
- `VAULT_DEFAULT_WAKE_INTERVAL_SEC`: 60 (spec §6).
- `VAULT_I2C_ADDR`: `0x42` (7-bit I2C slave address).
- Register addresses are fixed regardless of `VAULT_CONTEXT_SIZE` (spec §5, as amended): `STATUS=0x00`, `PROTOCOL_VERSION=0x01`, `CONTEXT_LENGTH=0x02`, `CONTEXT_DATA=0x03`, `COMMAND=0x04`, `WAKE_INTERVAL_SEC=0x05`. `VAULT_PROTOCOL_VERSION = 0x01`. `VAULT_CMD_DONE = 0x01`.
- All multi-byte register fields are little-endian.
- `core/` never includes a vendor SDK header or anything from `vendor/`.
- No git commit includes a "Co-Authored-By" trailer (repo-wide rule from the user).
- LPC810: 4 KB flash / 1 KB SRAM. STM32U031F8P6: 64 KB flash / 12 KB SRAM.

---

## Phase A — Platform-agnostic core (host-testable)

### Task 1: Repository scaffolding, HAL contract, top-level build

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/toolchain-arm-none-eabi.cmake`
- Create: `core/include/vault/platform.h`
- Create: `.gitignore`

**Interfaces:**
- Produces: `platform.h` contract — `platform_init()`, `platform_wakeup_timer_arm(uint32_t seconds)`, `platform_wakeup_timer_clear(void)`, `platform_main_rail_enable(bool on)`, `platform_i2c_slave_init(uint8_t addr)`, `platform_i2c_slave_deinit(void)`, `platform_bus_isolate(void)`, `platform_enter_low_power_sleep(void)`. Every later backend task implements these exact signatures.
- Produces: CMake variable `VAULT_TARGET` (`host`/`lpc810`/`stm32u031`) and `VAULT_CONTEXT_SIZE`, consumed by Task 4 (`core/CMakeLists.txt`) and every backend's `CMakeLists.txt`.

- [ ] **Step 1: Create `.gitignore`**

```
build/
*.o
*.elf
*.bin
*.map
```

- [ ] **Step 2: Create the HAL contract header**

`core/include/vault/platform.h`:
```c
#ifndef VAULT_PLATFORM_H
#define VAULT_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

/* Implemented once per backend (platform/lpc810, platform/stm32u031,
   platform/host_mock). core/ calls only these functions and never
   includes anything backend- or vendor-specific. */

void platform_init(void);

/* Arms the wakeup timer/RTC to fire in `seconds` seconds. */
void platform_wakeup_timer_arm(uint32_t seconds);
void platform_wakeup_timer_clear(void);

/* Turns the main Application Processor's power rail on/off. */
void platform_main_rail_enable(bool on);

/* Starts/stops the I2C peripheral in slave mode at 7-bit address `addr`.
   While active, incoming bytes and read requests must be routed to
   vault_i2c_registers_on_write_byte() / on_read_request() / on_stop()
   (declared in vault/vault_i2c_registers.h), typically from the
   backend's I2C interrupt handler. */
void platform_i2c_slave_init(uint8_t addr);
void platform_i2c_slave_deinit(void);

/* Puts the I2C SDA/SCL pins into a high-impedance state (analog input,
   no pull-up/down) so a powered-down main MCU cannot back-power this
   MCU through its GPIO ESD diodes. Must be called after
   platform_i2c_slave_deinit() and before platform_main_rail_enable(false). */
void platform_bus_isolate(void);

/* Enters the deepest low-power mode that still retains SRAM and CPU
   register state, and blocks until the armed wakeup timer fires.
   Must return with execution continuing on the line after the call
   (no reset), consistent with STM32 Stop 2 / LPC81x Power-down mode. */
void platform_enter_low_power_sleep(void);

#endif /* VAULT_PLATFORM_H */
```

- [ ] **Step 3: Create the ARM cross-compile toolchain file**

`cmake/toolchain-arm-none-eabi.cmake`:
```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -nostartfiles")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

- [ ] **Step 4: Create the top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VAULT_TARGET)
    set(VAULT_TARGET "host" CACHE STRING "Build target: host, lpc810, stm32u031")
endif()

if(VAULT_TARGET STREQUAL "lpc810" OR VAULT_TARGET STREQUAL "stm32u031")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake" CACHE FILEPATH "")
endif()

project(lorawan_wakeup_manager C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED VAULT_CONTEXT_SIZE)
    if(VAULT_TARGET STREQUAL "lpc810")
        set(VAULT_CONTEXT_SIZE 64)
    elseif(VAULT_TARGET STREQUAL "stm32u031")
        set(VAULT_CONTEXT_SIZE 128)
    else()
        set(VAULT_CONTEXT_SIZE 8)
    endif()
endif()

enable_testing()

add_subdirectory(core)

if(VAULT_TARGET STREQUAL "host")
    add_subdirectory(platform/host_mock)
    add_subdirectory(tests)
elseif(VAULT_TARGET STREQUAL "lpc810")
    add_subdirectory(platform/lpc810)
elseif(VAULT_TARGET STREQUAL "stm32u031")
    add_subdirectory(platform/stm32u031)
else()
    message(FATAL_ERROR "Unknown VAULT_TARGET: ${VAULT_TARGET} (expected host, lpc810, or stm32u031)")
endif()
```

- [ ] **Step 5: Verify the configure step works (no targets exist yet, so just check CMake itself is happy)**

Run:
```bash
mkdir -p build/host && cd build/host && cmake ../.. -DVAULT_TARGET=host
```
Expected: CMake fails with `add_subdirectory` errors about missing `core/CMakeLists.txt` — this is expected at this point since Task 4 hasn't created it yet. Confirms the top-level file itself has no syntax errors (the error is specifically "CMakeLists.txt does not exist" for `core`, `platform/host_mock`, `tests`, not a parse error).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt cmake/ core/include/vault/platform.h .gitignore
git commit -m "Scaffold build system and HAL contract"
```

---

### Task 2: `vault_i2c_registers` — register map logic (TDD)

**Files:**
- Create: `core/include/vault/vault_i2c_registers.h`
- Create: `core/src/vault_i2c_registers.c`
- Create: `core/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_framework.h`
- Create: `tests/test_vault_i2c_registers.c`

**Interfaces:**
- Consumes: `VAULT_CONTEXT_SIZE` (compile definition, set by `core/CMakeLists.txt` from the top-level CMake variable of the same name).
- Produces (used by Task 3's `host_mock` and Task 4's `vault_core`): `void vault_i2c_registers_reset_for_cycle(void)`, `void vault_i2c_registers_on_write_byte(uint8_t byte)`, `uint8_t vault_i2c_registers_on_read_request(void)`, `void vault_i2c_registers_on_stop(void)`, `bool vault_i2c_registers_done_requested(void)`, `bool vault_state_context_valid(void)`, `uint32_t vault_state_wake_interval_sec(void)`, `void vault_state_set_wake_interval_sec(uint32_t seconds)`, `void vault_test_reset_all(void)`. Also the register/command constants: `VAULT_REG_STATUS`, `VAULT_REG_PROTOCOL_VERSION`, `VAULT_REG_CONTEXT_LENGTH`, `VAULT_REG_CONTEXT_DATA`, `VAULT_REG_COMMAND`, `VAULT_REG_WAKE_INTERVAL_SEC`, `VAULT_CMD_DONE`, `VAULT_PROTOCOL_VERSION`, `VAULT_STATUS_CONTEXT_VALID_BIT`.

- [ ] **Step 1: Create the test framework header**

`tests/test_framework.h`:
```c
#ifndef VAULT_TEST_FRAMEWORK_H
#define VAULT_TEST_FRAMEWORK_H

#include <stdio.h>

static int g_test_failures = 0;
static int g_test_count = 0;

#define TEST_ASSERT(cond) \
    do { \
        g_test_count++; \
        if (!(cond)) { \
            g_test_failures++; \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual) \
    do { \
        g_test_count++; \
        long _e = (long)(expected); \
        long _a = (long)(actual); \
        if (_e != _a) { \
            g_test_failures++; \
            printf("FAIL: %s:%d: expected %ld, got %ld\n", __FILE__, __LINE__, _e, _a); \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        printf("RUN  %s\n", #fn); \
        fn(); \
    } while (0)

#endif /* VAULT_TEST_FRAMEWORK_H */
```

- [ ] **Step 2: Create the register map header**

`core/include/vault/vault_i2c_registers.h`:
```c
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
```

- [ ] **Step 3: Write the failing tests**

`tests/test_vault_i2c_registers.c`:
```c
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

static void test_context_length_roundtrip_and_clamp(void) {
    vault_test_reset_all();
    uint8_t write_bytes[] = { VAULT_REG_CONTEXT_LENGTH, 3 };
    simulate_write_transaction(write_bytes, sizeof(write_bytes));

    uint8_t readback;
    simulate_read(VAULT_REG_CONTEXT_LENGTH, &readback, 1);
    TEST_ASSERT_EQ_INT(3, readback);

    uint8_t oversized[] = { VAULT_REG_CONTEXT_LENGTH, 200 };
    simulate_write_transaction(oversized, sizeof(oversized));
    simulate_read(VAULT_REG_CONTEXT_LENGTH, &readback, 1);
    TEST_ASSERT_EQ_INT(VAULT_CONTEXT_SIZE, readback);
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

int main(void) {
    RUN_TEST(test_status_starts_invalid);
    RUN_TEST(test_protocol_version);
    RUN_TEST(test_context_length_roundtrip_and_clamp);
    RUN_TEST(test_context_data_roundtrip_and_valid_flag);
    RUN_TEST(test_context_data_write_beyond_size_is_clamped);
    RUN_TEST(test_command_done_only_after_stop);
    RUN_TEST(test_done_requested_resets_for_new_cycle);
    RUN_TEST(test_wake_interval_roundtrip_little_endian);
    RUN_TEST(test_wake_interval_partial_write_does_not_commit);
    printf("%d/%d tests passed\n", g_test_count - g_test_failures, g_test_count);
    return g_test_failures != 0;
}
```

- [ ] **Step 4: Create `core/CMakeLists.txt` and `tests/CMakeLists.txt` (needed to build the test, even though `vault_i2c_registers.c` doesn't exist yet)**

`core/CMakeLists.txt`:
```cmake
add_library(vault_core STATIC
    src/vault_i2c_registers.c
    src/vault_core.c
)

target_include_directories(vault_core PUBLIC include)

target_compile_definitions(vault_core PUBLIC
    VAULT_CONTEXT_SIZE=${VAULT_CONTEXT_SIZE}
    VAULT_I2C_ADDR=0x42
    VAULT_DEFAULT_WAKE_INTERVAL_SEC=60
)
```

`tests/CMakeLists.txt`:
```cmake
add_executable(test_vault_i2c_registers test_vault_i2c_registers.c)
target_link_libraries(test_vault_i2c_registers PRIVATE vault_core)
add_test(NAME vault_i2c_registers COMMAND test_vault_i2c_registers)
```

Also create an empty placeholder `core/src/vault_core.c` containing just `/* placeholder, implemented in Task 4 */` and nothing else, so this task's build succeeds — Task 4 replaces its contents.

- [ ] **Step 5: Create an empty `vault_i2c_registers.c` stub and run the test to verify it fails**

`core/src/vault_i2c_registers.c` (stub — real implementation is the next step):
```c
#include "vault/vault_i2c_registers.h"

void vault_i2c_registers_on_write_byte(uint8_t byte) { (void)byte; }
uint8_t vault_i2c_registers_on_read_request(void) { return 0xFFu; }
void vault_i2c_registers_on_stop(void) {}
void vault_i2c_registers_reset_for_cycle(void) {}
bool vault_i2c_registers_done_requested(void) { return false; }
bool vault_state_context_valid(void) { return false; }
uint32_t vault_state_wake_interval_sec(void) { return 0; }
void vault_state_set_wake_interval_sec(uint32_t seconds) { (void)seconds; }
void vault_test_reset_all(void) {}
```

Run:
```bash
mkdir -p build/host && cd build/host && cmake ../.. -DVAULT_TARGET=host && cmake --build . && ctest --output-on-failure
```
Expected: build succeeds, `ctest` reports FAIL for `vault_i2c_registers` with several `FAIL:` lines from the stub's wrong behavior (e.g. protocol version mismatch, status bit mismatch).

- [ ] **Step 6: Implement `vault_i2c_registers.c` for real**

`core/src/vault_i2c_registers.c`:
```c
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
```

- [ ] **Step 7: Run the tests and verify they pass**

Run:
```bash
cd build/host && cmake --build . && ctest --output-on-failure
```
Expected: `100% tests passed` for `vault_i2c_registers`, all `9/9 tests passed` printed by the test binary.

- [ ] **Step 8: Commit**

```bash
git add core/ tests/ CMakeLists.txt
git commit -m "Implement I2C register-map protocol logic with unit tests"
```

---

### Task 3: `host_mock` platform backend

**Files:**
- Create: `platform/host_mock/include/vault/host_mock_test_api.h`
- Create: `platform/host_mock/src/platform_host_mock.c`
- Create: `platform/host_mock/CMakeLists.txt`

**Interfaces:**
- Consumes: `platform.h` (Task 1), `vault_i2c_registers.h` hooks (Task 2).
- Produces (used by Task 4's tests): `host_mock_call_t` enum (`HOST_MOCK_CALL_MAIN_RAIL_ENABLE`, `HOST_MOCK_CALL_I2C_SLAVE_INIT`, `HOST_MOCK_CALL_I2C_SLAVE_DEINIT`, `HOST_MOCK_CALL_BUS_ISOLATE`, `HOST_MOCK_CALL_WAKEUP_TIMER_ARM`, `HOST_MOCK_CALL_WAKEUP_TIMER_CLEAR`, `HOST_MOCK_CALL_ENTER_LOW_POWER_SLEEP`), `host_mock_call_record_t { host_mock_call_t call; uint32_t arg; }`, `void host_mock_reset(void)`, `size_t host_mock_call_count(void)`, `const host_mock_call_record_t *host_mock_call_at(size_t index)`, `void host_mock_queue_write_transaction(const uint8_t *bytes, size_t count)`.

- [ ] **Step 1: Create the test-only mock API header**

`platform/host_mock/include/vault/host_mock_test_api.h`:
```c
#ifndef VAULT_HOST_MOCK_TEST_API_H
#define VAULT_HOST_MOCK_TEST_API_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HOST_MOCK_CALL_MAIN_RAIL_ENABLE,
    HOST_MOCK_CALL_I2C_SLAVE_INIT,
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
   time platform_i2c_slave_init() is called. Call multiple times to queue
   several transactions, replayed in order. If a test wants vault_core's
   wait loop to exit, at least one queued transaction must write
   VAULT_CMD_DONE to VAULT_REG_COMMAND -- this mock is fully synchronous,
   so nothing else will ever set done_requested after init() returns. */
void host_mock_queue_write_transaction(const uint8_t *bytes, size_t count);

#endif /* VAULT_HOST_MOCK_TEST_API_H */
```

- [ ] **Step 2: Implement the mock**

`platform/host_mock/src/platform_host_mock.c`:
```c
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
```

- [ ] **Step 3: Create `platform/host_mock/CMakeLists.txt`**

```cmake
add_library(vault_platform_host_mock STATIC
    src/platform_host_mock.c
)

target_include_directories(vault_platform_host_mock PUBLIC include)
target_link_libraries(vault_platform_host_mock PUBLIC vault_core)
```

- [ ] **Step 4: Verify it builds (no consumer yet, so build the object directly)**

Run:
```bash
cd build/host && cmake ../.. -DVAULT_TARGET=host && cmake --build . --target vault_platform_host_mock
```
Expected: builds successfully with no warnings.

- [ ] **Step 5: Commit**

```bash
git add platform/host_mock/
git commit -m "Add host_mock platform backend for testing"
```

---

### Task 4: `vault_core` state machine (TDD)

**Files:**
- Create: `core/include/vault/vault_core.h`
- Modify: `core/src/vault_core.c` (replace Task 2's placeholder)
- Create: `tests/test_vault_core.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `platform.h` (Task 1), `vault_i2c_registers.h` (Task 2), `host_mock_test_api.h` (Task 3).
- Produces: `void vault_core_init(void)`, `void vault_core_step(void)`. `main()` on real hardware (Tasks 10 and 15) calls `vault_core_init()` once, then `vault_core_step()` in an infinite loop.

- [ ] **Step 1: Create `core/include/vault/vault_core.h`**

```c
#ifndef VAULT_CORE_H
#define VAULT_CORE_H

/* Called exactly once, after a genuine cold boot/reset. Must not be
   called again on wake from platform_enter_low_power_sleep(), since
   that call does not return through here (see vault_core_step()). */
void vault_core_init(void);

/* Runs one full WAKE_MAIN -> BUS_ISOLATION -> ARM_SLEEP cycle, ending
   with platform_enter_low_power_sleep(). Returns once that call returns
   (i.e. once the wakeup timer has fired), ready for the next cycle. */
void vault_core_step(void);

#endif /* VAULT_CORE_H */
```

- [ ] **Step 2: Write the failing tests**

`tests/test_vault_core.c`:
```c
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
```

- [ ] **Step 3: Add the test executable to `tests/CMakeLists.txt`**

Append to `tests/CMakeLists.txt`:
```cmake
add_executable(test_vault_core test_vault_core.c)
target_link_libraries(test_vault_core PRIVATE vault_core vault_platform_host_mock)
add_test(NAME vault_core COMMAND test_vault_core)
```

- [ ] **Step 4: Run the tests to verify they fail**

Run:
```bash
cd build/host && cmake ../.. -DVAULT_TARGET=host && cmake --build . && ctest --output-on-failure -R vault_core
```
Expected: build fails to link `test_vault_core` — `vault_core_init`/`vault_core_step` are undefined (Task 2's placeholder `vault_core.c` is empty).

- [ ] **Step 5: Implement `vault_core.c`**

Replace `core/src/vault_core.c` entirely with:
```c
#include "vault/vault_core.h"
#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"

void vault_core_init(void) {
    vault_state_set_wake_interval_sec(VAULT_DEFAULT_WAKE_INTERVAL_SEC);
}

void vault_core_step(void) {
    /* WAKE_MAIN */
    platform_main_rail_enable(true);
    vault_i2c_registers_reset_for_cycle();
    platform_i2c_slave_init(VAULT_I2C_ADDR);

    while (!vault_i2c_registers_done_requested()) {
        /* Busy-wait. Real backends service I2C via an interrupt handler
           that calls vault_i2c_registers_on_*() concurrently with this
           loop; see platform/lpc810 and platform/stm32u031. */
    }

    /* BUS_ISOLATION */
    platform_i2c_slave_deinit();
    platform_bus_isolate();
    platform_main_rail_enable(false);

    /* ARM_SLEEP */
    platform_wakeup_timer_arm(vault_state_wake_interval_sec());
    platform_enter_low_power_sleep();
}
```

- [ ] **Step 6: Run the tests and verify they pass**

Run:
```bash
cd build/host && cmake --build . && ctest --output-on-failure
```
Expected: `100% tests passed`, both `vault_i2c_registers` and `vault_core` suites green.

- [ ] **Step 7: Commit**

```bash
git add core/ tests/
git commit -m "Implement vault_core wake/sleep state machine"
```

---

## Phase B — LPC810 backend (real hardware, available now)

> **Note on hardware-register accuracy:** the tasks below use the standard LPC81x CMSIS register struct names (`LPC_SYSCON`, `LPC_GPIO_PORT`, `LPC_SWM`, `LPC_WKT`, `LPC_I2C0`, `LPC_PMU`) and their documented bit meanings. Before flashing to real hardware, cross-check every register/bit referenced in a code comment as "verify against UM10601 §N" against the actual NXP UM10601 LPC81x User Manual — legacy low-volume parts like this are the case where trusting a register value without the manual open next to it is how boards get bricked or, worse, GPIO pins get driven in a way that back-feeds the battery rail this whole project exists to protect.

### Task 5: Vendor CMSIS core and the LPC81x device header

**Files:**
- Create: `vendor/CMSIS_5` (git submodule)
- Create: `platform/lpc810/vendor/LPC8xx.h`
- Create: `platform/lpc810/CMakeLists.txt`
- Create: `platform/lpc810/src/smoke_test_main.c`

**Interfaces:**
- Produces: `#include "LPC8xx.h"` (device register definitions: `LPC_SYSCON`, `LPC_GPIO_PORT`, `LPC_SWM`, `LPC_WKT`, `LPC_I2C0`, `LPC_PMU`, `NVIC_*`) available to every later LPC810 task via `platform/lpc810/CMakeLists.txt`'s include path.

- [ ] **Step 1: Add the CMSIS core submodule, pinned to release 5.9.0**

```bash
git submodule add https://github.com/ARM-software/CMSIS_5.git vendor/CMSIS_5
cd vendor/CMSIS_5 && git checkout 5.9.0 && cd ../..
git add vendor/CMSIS_5 .gitmodules
```

- [ ] **Step 2: Vendor the NXP LPC8xx CMSIS device header**

NXP's own `mcux-devices-lpc` GitHub repo no longer carries the original LPC81x family (LPC810/811/812) — only newer LPC80x parts (LPC802 and up). The canonical redistributable source for the original device header is the `Keil.LPC800_DFP` CMSIS-Pack (NXP-authored, distributed via ARM's Keil pack index):

```bash
mkdir -p /tmp/lpc800_dfp && cd /tmp/lpc800_dfp
curl -L -o Keil.LPC800_DFP.1.2.0.pack https://www.keil.com/dd2/GetPack/Keil.LPC800_DFP.1.2.0.pack
unzip -o Keil.LPC800_DFP.1.2.0.pack -d extracted
find extracted -iname 'LPC8xx.h'
```

Expected: `find` prints one path under `extracted/Device/` (or similar; the pack's internal layout may put it a directory or two deeper — use whatever path `find` reports). Copy that exact file, unmodified, into the repo:

```bash
cp extracted/<path-found-above>/LPC8xx.h /path/to/lorawan-wakeup-manager/platform/lpc810/vendor/LPC8xx.h
```

If the `curl` URL 404s (pack versions can be renumbered), fetch `http://www.keil.com/pack/Keil.LPC800_DFP.pdsc` first, find the current version number in it, and substitute that version into the pack URL.

- [ ] **Step 3: Create `platform/lpc810/CMakeLists.txt` with a smoke-test executable**

```cmake
add_library(vault_lpc810_cmsis_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_lpc810_cmsis_check PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
)

target_compile_options(vault_lpc810_cmsis_check PRIVATE -mcpu=cortex-m0plus -mthumb)
```

- [ ] **Step 4: Create a trivial file that only proves the headers parse and the struct names exist**

`platform/lpc810/src/smoke_test_main.c`:
```c
#include "LPC8xx.h"

/* Referencing these pointers (without dereferencing) is enough to prove
   the vendored header defines the peripheral base addresses this
   project needs later, and that it parses cleanly under
   arm-none-eabi-gcc -mcpu=cortex-m0plus. */
static void *const s_peripheral_check[] = {
    (void *)LPC_SYSCON,
    (void *)LPC_GPIO_PORT,
    (void *)LPC_SWM,
    (void *)LPC_WKT,
    (void *)LPC_I2C0,
};

int lpc810_cmsis_check_reference(void) {
    return (int)(intptr_t)s_peripheral_check[0];
}
```

- [ ] **Step 5: Build the smoke-test target and verify it compiles**

Run:
```bash
mkdir -p build/lpc810 && cd build/lpc810
cmake ../.. -DVAULT_TARGET=lpc810
cmake --build . --target vault_lpc810_cmsis_check
```
Expected: compiles with no errors. If `LPC_SYSCON`/`LPC_GPIO_PORT`/`LPC_SWM`/`LPC_WKT`/`LPC_I2C0` are undefined, the vendored header's actual macro names differ slightly from these — open `platform/lpc810/vendor/LPC8xx.h` and grep for `#define LPC_` to find the exact names, then update this list to match before proceeding to Task 7.

- [ ] **Step 6: Commit**

```bash
git add vendor/CMSIS_5 .gitmodules platform/lpc810/vendor/LPC8xx.h platform/lpc810/CMakeLists.txt platform/lpc810/src/smoke_test_main.c
git commit -m "Vendor CMSIS core and LPC8xx device header for LPC810 backend"
```

---

### Task 6: LPC810 linker script, startup file, and CMake target

**Files:**
- Create: `platform/lpc810/linker/lpc810.ld`
- Create: `platform/lpc810/src/startup_lpc810.c`
- Modify: `platform/lpc810/CMakeLists.txt`
- Create: `platform/lpc810/src/main.c` (empty `main()` for now; filled in by Task 10)

**Interfaces:**
- Produces: the `vault_lpc810` executable CMake target that later tasks (7-10) add source files to; the vector table's `Reset_Handler` symbol that later becomes the actual entry point.

- [ ] **Step 1: Create the linker script for the LPC810's 4 KB flash / 1 KB SRAM**

`platform/lpc810/linker/lpc810.ld`:
```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4K
    RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 1K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + 256; /* minimal stack reservation for this bare-metal loop */
        . = ALIGN(8);
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

- [ ] **Step 2: Create the startup file (vector table + `Reset_Handler`)**

`platform/lpc810/src/startup_lpc810.c`:
```c
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void WKT_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C0_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0, /* reserved */
    SVC_Handler,
    0, 0, /* reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* IRQ0-31 -- only the two this project uses are named; the rest
       fall through to Default_Handler via the weak aliases above once
       CMSIS's actual IRQn_Type enum order is confirmed in Task 8/9
       against LPC8xx.h (the position of WKT_IRQn and I2C0_IRQn in that
       enum must match their position in this table -- verify before
       relying on either interrupt firing). */
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }
    main();
    while (1) { }
}

void Default_Handler(void) {
    while (1) { }
}
```

- [ ] **Step 3: Create a placeholder `main.c`**

`platform/lpc810/src/main.c`:
```c
int main(void) {
    while (1) { }
}
```

- [ ] **Step 4: Add the real executable target to `platform/lpc810/CMakeLists.txt`**

Append:
```cmake
add_executable(vault_lpc810
    src/startup_lpc810.c
    src/main.c
)

target_include_directories(vault_lpc810 PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
)

target_compile_options(vault_lpc810 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_lpc810 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc810.ld
    -Wl,-Map=vault_lpc810.map
)

add_custom_command(TARGET vault_lpc810 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_lpc810>
)
```

- [ ] **Step 5: Build and verify it fits the 4 KB flash / 1 KB SRAM budget**

Run:
```bash
cd build/lpc810 && cmake --build . --target vault_lpc810
```
Expected: link succeeds, and the `arm-none-eabi-size` output shows `text` (flash usage) well under 4096 bytes and `data + bss` well under 1024 bytes for this near-empty program.

- [ ] **Step 6: Commit**

```bash
git add platform/lpc810/linker platform/lpc810/src/startup_lpc810.c platform/lpc810/src/main.c platform/lpc810/CMakeLists.txt
git commit -m "Add LPC810 linker script, startup code, and executable target"
```

---

### Task 7: LPC810 GPIO — main rail enable and bus isolation

**Files:**
- Create: `platform/lpc810/src/platform_lpc810_gpio.c`
- Modify: `platform/lpc810/CMakeLists.txt`

**Interfaces:**
- Consumes: `LPC8xx.h` register structs (Task 5), `platform.h` (Task 1).
- Produces: `platform_main_rail_enable(bool on)`, `platform_bus_isolate(void)`, plus an internal `void lpc810_gpio_init(void)` called from `platform_init()` (Task 10).

Pin assignment for this task (fixed for the LPC810 backend): `PIO0_0` drives the main rail's regulator-enable line; `PIO0_10`/`PIO0_11` are the I2C SDA/SCL pins reconfigured to analog/no-pull for isolation. **Verify these three pin numbers against your actual schematic before flashing** — they are placeholders consistent with the LPC810's 6 usable I/Os, not a specific board's wiring.

- [ ] **Step 1: Implement GPIO setup, rail control, and bus isolation**

`platform/lpc810/src/platform_lpc810_gpio.c`:
```c
#include "vault/platform.h"
#include "LPC8xx.h"

#define MAIN_RAIL_EN_PIN   0u  /* PIO0_0 -- verify against schematic */
#define I2C_SDA_PIN       10u  /* PIO0_10 -- verify against schematic */
#define I2C_SCL_PIN       11u  /* PIO0_11 -- verify against schematic */

void lpc810_gpio_init(void) {
    /* Enable clocks to GPIO and IOCON. Bit positions per UM10601
       Table "SYSAHBCLKCTRL register bit description" -- verify before
       flashing; this uses the commonly-documented LPC81x assignment
       (bit 6 = GPIO, bit 18 = IOCON). */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 6) | (1u << 18);

    /* Main rail enable pin: digital output, driven low (rail off) at boot. */
    LPC_GPIO_PORT->DIR0 |= (1u << MAIN_RAIL_EN_PIN);
    LPC_GPIO_PORT->CLR0 = (1u << MAIN_RAIL_EN_PIN);
}

void platform_main_rail_enable(bool on) {
    if (on) {
        LPC_GPIO_PORT->SET0 = (1u << MAIN_RAIL_EN_PIN);
    } else {
        LPC_GPIO_PORT->CLR0 = (1u << MAIN_RAIL_EN_PIN);
    }
}

void platform_bus_isolate(void) {
    /* IOCON PIO registers: MODE bits [4:3] = 00 selects no pull-up/down,
       and clearing the pin's function bits in the switch matrix (done in
       platform_i2c_slave_deinit(), Task 9) plus setting the pin to a
       GPIO input here leaves it as a plain high-impedance input with no
       pulls -- verify the exact IOCON bit layout against UM10601 Table
       "IOCON pin description" before flashing. */
    LPC_GPIO_PORT->DIR0 &= ~((1u << I2C_SDA_PIN) | (1u << I2C_SCL_PIN));
    LPC_IOCON->PIO0_10 &= ~(0x3u << 3); /* clear MODE bits: no pull-up/down */
    LPC_IOCON->PIO0_11 &= ~(0x3u << 3);
}
```

- [ ] **Step 2: Add the source file to the executable target**

In `platform/lpc810/CMakeLists.txt`, add `src/platform_lpc810_gpio.c` to `vault_lpc810`'s source list, and add a call to `lpc810_gpio_init();` at the top of `main()` in `platform/lpc810/src/main.c`.

- [ ] **Step 3: Build and verify it compiles**

Run:
```bash
cd build/lpc810 && cmake --build . --target vault_lpc810
```
Expected: compiles and links with no errors. If `LPC_IOCON` or field names (`PIO0_10`, `DIR0`, `SET0`, `CLR0`) don't match the vendored header exactly, grep `platform/lpc810/vendor/LPC8xx.h` for the real names and fix this file to match.

- [ ] **Step 4: Commit**

```bash
git add platform/lpc810/src/platform_lpc810_gpio.c platform/lpc810/src/main.c platform/lpc810/CMakeLists.txt
git commit -m "Implement LPC810 GPIO: main rail enable and I2C bus isolation"
```

---

### Task 8: LPC810 wakeup timer (WKT)

**Files:**
- Create: `platform/lpc810/src/platform_lpc810_timer.c`
- Modify: `platform/lpc810/CMakeLists.txt`, `platform/lpc810/src/main.c`

**Interfaces:**
- Produces: `platform_wakeup_timer_arm(uint32_t seconds)`, `platform_wakeup_timer_clear(void)`, internal `void lpc810_timer_init(void)`.

The LPC810 has no RTC peripheral — its self-Wake-up Timer (WKT) is the closest equivalent, and per the earlier design decision it runs from the internal low-power oscillator (not an external crystal), trading timing precision for freeing up pins.

- [ ] **Step 1: Implement the WKT-based wakeup timer**

`platform/lpc810/src/platform_lpc810_timer.c`:
```c
#include "vault/platform.h"
#include "LPC8xx.h"

/* WKT counts down from a loaded value at its clock source's rate.
   Internal low-power oscillator is nominally ~10 kHz on LPC81x parts
   -- verify the exact nominal frequency and its accuracy/thermal drift
   figures against UM10601 section "Self wake-up timer (WKT)" before
   relying on this for real interval timing; this is the RTC-timing
   limitation already flagged in the design spec as a known LPC810
   bring-up limitation (no external crystal). */
#define WKT_CLOCK_HZ 10000u

void lpc810_timer_init(void) {
    /* Enable clock to WKT. Bit position per UM10601 SYSAHBCLKCTRL table
       -- verify before flashing. */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 9);

    /* Select the internal low-power oscillator as the WKT clock source
       (as opposed to the external 32 kHz crystal input) -- verify the
       exact CTRL register bit/encoding against UM10601 "WKT Control
       register" before flashing. */
    LPC_WKT->CTRL = 0u;
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    uint32_t count = seconds * WKT_CLOCK_HZ;
    /* Writing COUNT starts the countdown; WKT_IRQHandler (startup_lpc810.c)
       fires when it reaches zero. Verify the COUNT register's start-on-write
       behavior against UM10601 before relying on it. */
    LPC_WKT->COUNT = count;
}

void platform_wakeup_timer_clear(void) {
    /* Clear the WKT alarm/interrupt flag. Verify the exact flag name and
       clear mechanism (write-1-to-clear vs read-to-clear) against
       UM10601 before relying on this in an ISR. */
    LPC_WKT->CTRL |= (1u << 1);
}
```

- [ ] **Step 2: Wire it into the build and `main()`**

Add `src/platform_lpc810_timer.c` to `vault_lpc810`'s sources in `platform/lpc810/CMakeLists.txt`, and call `lpc810_timer_init();` from `main()` alongside `lpc810_gpio_init();`.

- [ ] **Step 3: Build and verify it compiles**

Run:
```bash
cd build/lpc810 && cmake --build . --target vault_lpc810
```
Expected: compiles and links. Fix any `LPC_WKT` field-name mismatches against the vendored header as in Task 7.

- [ ] **Step 4: Commit**

```bash
git add platform/lpc810/src/platform_lpc810_timer.c platform/lpc810/src/main.c platform/lpc810/CMakeLists.txt
git commit -m "Implement LPC810 wakeup timer (WKT) backend"
```

---

### Task 9: LPC810 I2C0 slave driver

**Files:**
- Create: `platform/lpc810/src/platform_lpc810_i2c.c`
- Modify: `platform/lpc810/CMakeLists.txt`, `platform/lpc810/src/startup_lpc810.c`, `platform/lpc810/src/main.c`

**Interfaces:**
- Consumes: `vault_i2c_registers_on_write_byte`, `vault_i2c_registers_on_read_request`, `vault_i2c_registers_on_stop` (Task 2 — same functions the host_mock backend drives).
- Produces: `platform_i2c_slave_init(uint8_t addr)`, `platform_i2c_slave_deinit(void)`, `I2C0_IRQHandler(void)` (replaces the weak default from Task 6).

This is the task most in need of the UM10601 cross-check called out at the top of Phase B — the LPC81x I2C0 peripheral's slave-mode state machine (`STAT.SLVSTATE`, `SLVCTL.SLVCONTINUE`) is genuinely silicon-specific and must be verified against the manual's I2C chapter, not just this code, before flashing.

- [ ] **Step 1: Implement the I2C0 slave driver**

`platform/lpc810/src/platform_lpc810_i2c.c`:
```c
#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "LPC8xx.h"

/* SWM fixed-function/movable pin assignment for I2C0 SDA/SCL to
   PIO0_10/PIO0_11. Verify the exact PINASSIGN register index and byte
   offset against UM10601 "Switch Matrix" chapter before flashing --
   I2C0 SDA/SCL are fixed-function pins on some LPC81x parts and
   movable on others. */
static void lpc810_i2c_pins_to_i2c_function(void) {
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 7); /* enable SWM clock -- verify bit */
    /* Actual PINASSIGN/PINENABLE register writes selecting I2C0 function
       on PIO0_10/PIO0_11 go here once confirmed against UM10601. */
}

void platform_i2c_slave_init(uint8_t addr) {
    LPC_SYSCON->SYSAHBCLKCTRL |= (1u << 5); /* enable I2C0 clock -- verify bit */
    lpc810_i2c_pins_to_i2c_function();

    /* Slave address 0 register: 7-bit address in bits [7:1], bit 0 is
       the SLVEN enable bit -- verify field layout against UM10601
       "Slave Address 0 register" before flashing. */
    LPC_I2C0->SLVADR0 = (uint32_t)((addr << 1) | 0x01u);

    /* Enable I2C0 slave function and its interrupt. Verify CFG.SLVEN
       bit position and enable the NVIC line for I2C0_IRQn (confirm its
       IRQn_Type value in LPC8xx.h matches the vector table position in
       startup_lpc810.c, per the note left there in Task 6). */
    LPC_I2C0->CFG |= (1u << 1);
    LPC_I2C0->INTENSET |= (1u << 8); /* SLVPENDING interrupt enable -- verify bit */

    NVIC_EnableIRQ(I2C0_IRQn);
}

void platform_i2c_slave_deinit(void) {
    NVIC_DisableIRQ(I2C0_IRQn);
    LPC_I2C0->CFG &= ~(1u << 1);
    LPC_I2C0->INTENCLR |= (1u << 8);
    LPC_SYSCON->SYSAHBCLKCTRL &= ~(1u << 5);
    /* Returning the pins to plain GPIO (undoing lpc810_i2c_pins_to_i2c_function)
       happens here; platform_bus_isolate() (Task 7) then sets them to
       analog/no-pull. Verify the SWM "unassign" encoding against UM10601. */
}

void I2C0_IRQHandler(void) {
    /* Verify this whole handler's state-decoding logic against UM10601
       "I2C slave state codes" (STAT.SLVSTATE encodes ADDR/RX/TX; this
       sketch assumes 0=ADDR, 1=RX, 2=TX, matching the commonly
       documented LPC81x encoding, but confirm before relying on it). */
    uint32_t stat = LPC_I2C0->STAT;
    if (!(stat & (1u << 8))) { /* SLVPENDING bit -- verify position */
        return;
    }

    uint32_t slvstate = (stat >> 5) & 0x3u; /* SLVSTATE bits -- verify position */

    switch (slvstate) {
    case 0u: /* address match */
        (void)LPC_I2C0->SLVDAT; /* clears the address-match condition on some parts -- verify */
        break;
    case 1u: /* slave receive: master is writing a byte to us */
        vault_i2c_registers_on_write_byte((uint8_t)LPC_I2C0->SLVDAT);
        break;
    case 2u: /* slave transmit: master is reading a byte from us */
        LPC_I2C0->SLVDAT = vault_i2c_registers_on_read_request();
        break;
    default:
        break;
    }

    LPC_I2C0->SLVCTL |= (1u << 0); /* SLVCONTINUE -- verify bit position */
}
```

- [ ] **Step 2: Declare `I2C0_IRQHandler` as strong (remove its weak alias reliance)**

`I2C0_IRQHandler` is already declared `weak` in `startup_lpc810.c` (Task 6); defining it here overrides that at link time automatically — no edit to `startup_lpc810.c` needed. Confirm this by checking the map file in the next step shows the real handler's address in the vector table, not `Default_Handler`'s.

- [ ] **Step 3: Wire the STOP condition into the driver and into `vault_core`'s expectations**

The LPC81x I2C0 slave state machine (as sketched above) does not have an explicit "bus STOP" state code distinct from a fresh address match — a new address match after a prior transaction effectively signals the previous one ended. Add a call to `vault_i2c_registers_on_stop()` inside the `case 0u:` (address match) branch, guarded so it only fires when this is a *new* transaction rather than the first ever address match:

```c
static bool s_transaction_in_progress;

/* Inside I2C0_IRQHandler's case 0u: */
case 0u:
    if (s_transaction_in_progress) {
        vault_i2c_registers_on_stop();
    }
    s_transaction_in_progress = true;
    (void)LPC_I2C0->SLVDAT;
    break;
```

Also add a `LPC_I2C0->INTENSET |= (1u << ...);`-enabled **bus STOP detected** interrupt if UM10601 documents one (many I2C peripherals expose this separately from SLVPENDING) — verify this against the manual, since address-match-implies-previous-stop is a reasonable fallback but a real STOP-detect interrupt would be more precise and is worth using if available. If found, call `vault_i2c_registers_on_stop()` there instead and remove the `s_transaction_in_progress` workaround.

- [ ] **Step 4: Add the source file to the build**

Add `src/platform_lpc810_i2c.c` to `vault_lpc810`'s sources in `platform/lpc810/CMakeLists.txt`.

- [ ] **Step 5: Build and verify it compiles**

Run:
```bash
cd build/lpc810 && cmake --build . --target vault_lpc810
```
Expected: compiles and links. Resolve any register/field name mismatches against the vendored header.

- [ ] **Step 6: Commit**

```bash
git add platform/lpc810/src/platform_lpc810_i2c.c platform/lpc810/CMakeLists.txt
git commit -m "Implement LPC810 I2C0 slave driver (needs UM10601 verification before hardware bring-up)"
```

---

### Task 10: LPC810 sleep entry and full `main()` wiring

**Files:**
- Create: `platform/lpc810/src/platform_lpc810_power.c`
- Modify: `platform/lpc810/src/main.c`
- Modify: `platform/lpc810/CMakeLists.txt`

**Interfaces:**
- Produces: `platform_init(void)`, `platform_enter_low_power_sleep(void)`. `main()` now calls `vault_core_init()` once and loops `vault_core_step()` forever, per spec §6.

- [ ] **Step 1: Implement Power-down mode entry**

`platform/lpc810/src/platform_lpc810_power.c`:
```c
#include "vault/platform.h"
#include "LPC8xx.h"

void platform_init(void) {
    extern void lpc810_gpio_init(void);
    extern void lpc810_timer_init(void);
    lpc810_gpio_init();
    lpc810_timer_init();
}

void platform_enter_low_power_sleep(void) {
    /* PDRUNCFG selects which power domains stay alive in the sleep mode
       entered by WFI when SCR.SLEEPDEEP is set. Power-down mode (as
       opposed to Deep power-down) retains SRAM and resumes execution
       after WFI rather than resetting -- verify the exact PDRUNCFG bit
       pattern for "Power-down, SRAM retained, WKT running" against
       UM10601 "Power configuration register" before flashing; a wrong
       bit here can silently fall back to Deep power-down, which DOES
       reset on wake and would break vault_core's resume-in-place
       assumption. */
    LPC_SYSCON->PDRUNCFG = 0xFFFFFFFFu; /* placeholder pattern -- MUST be
                                            replaced with the verified
                                            Power-down bit pattern before
                                            this is flashed to hardware */

    SCB->SCR |= (1u << 2); /* SLEEPDEEP bit -- verify against ARM CMSIS core header, not UM10601 */

    __asm volatile ("wfi");
}
```

The `PDRUNCFG` line is intentionally left as an explicit placeholder value with a loud comment, rather than a guessed bit pattern presented as fact — this is the one register value in this plan that directly gates whether the retained-SRAM design assumption (spec §4, §6) holds at all, so guessing here would be worse than flagging it. Resolve it from UM10601 before Task 11's hardware bring-up.

- [ ] **Step 2: Wire the full state machine into `main()`**

Replace `platform/lpc810/src/main.c` entirely with:
```c
#include "vault/vault_core.h"
#include "vault/platform.h"

int main(void) {
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
```

- [ ] **Step 3: Add the new source file and link `vault_core` into the executable**

In `platform/lpc810/CMakeLists.txt`, add `src/platform_lpc810_power.c` to `vault_lpc810`'s sources, and add:
```cmake
target_link_libraries(vault_lpc810 PRIVATE vault_core)
```

- [ ] **Step 4: Build and verify it compiles and fits the memory budget**

Run:
```bash
cd build/lpc810 && cmake --build . --target vault_lpc810
```
Expected: builds successfully; `arm-none-eabi-size` output still comfortably under the 4 KB flash / 1 KB SRAM budget (the whole `vault_core` static library plus all LPC810 drivers should still be well under a kilobyte of flash for code this small).

- [ ] **Step 5: Commit**

```bash
git add platform/lpc810/src/platform_lpc810_power.c platform/lpc810/src/main.c platform/lpc810/CMakeLists.txt
git commit -m "Wire vault_core into LPC810 main() with Power-down sleep entry"
```

---

### Task 11: LPC810 hardware bring-up verification (manual, real board)

**Files:** none (verification task, no code changes expected unless bugs are found).

This is the task that fulfills the design spec's requirement that the LPC810 backend be "built, flashed, and validated on real hardware in this phase" (spec §7) — everything before this task only proves the code compiles.

- [ ] **Step 1: Resolve every "verify against UM10601" comment left in Tasks 7-10**

Open UM10601 (LPC81x User Manual) and confirm, updating the code where wrong: `SYSAHBCLKCTRL` bit positions for GPIO/IOCON/WKT/SWM/I2C0; `IOCON` MODE field layout; WKT `CTRL`/`COUNT` register behavior and the internal oscillator's nominal frequency; I2C0 `SLVADR0`/`CFG`/`INTENSET`/`STAT`/`SLVCTL`/`SLVDAT` bit fields and slave state-machine encoding, and whether a dedicated bus-STOP interrupt exists; `PDRUNCFG`'s Power-down-mode bit pattern (the placeholder from Task 10, Step 1).

- [ ] **Step 2: Flash and verify GPIO (Task 7)**

Flash `vault_lpc810.elf` (or the `.bin` produced via `arm-none-eabi-objcopy -O binary`) via your ISP/SWD programmer. With a multimeter or logic analyzer on the main-rail-enable pin, confirm it toggles high/low in time with the ~60-second default cycle (visible as a slow square wave).

- [ ] **Step 3: Verify bus isolation (Task 7) with the main MCU rail actually removed**

With a second board (or bench supply) standing in for the main MCU, confirm that when the main rail is off, the I2C SDA/SCL lines read as high-impedance (no current sourced/sunk by the LPC810) using a multimeter in current mode between the pull-up supply and the bus — this is the specific failure mode (parasitic back-powering) the design spec's electrical mitigation section exists to prevent.

- [ ] **Step 4: Verify the I2C0 slave driver (Task 9) with a real I2C master**

Using a second microcontroller, a Bus Pirate, or similar as the I2C master, perform each register access from the design spec's protocol table (§5) against the LPC810: read `STATUS`, `PROTOCOL_VERSION`; write and read back `CONTEXT_LENGTH`, `CONTEXT_DATA`, `WAKE_INTERVAL_SEC`; write `COMMAND = CMD_DONE` and confirm the LPC810 subsequently de-asserts the main rail and goes quiet (no further I2C ACKs) within roughly one second.

- [ ] **Step 5: Measure current in Power-down mode (Task 10)**

With the main rail confirmed off and the I2C bus isolated, measure the LPC810's own supply current during the sleep interval with a bench multimeter or a dedicated current-measurement tool (e.g. Otii, Joulescope, or a shunt + oscilloscope). Compare against the datasheet's Power-down-mode current figure — if it's dramatically higher, the `PDRUNCFG` bit pattern from Task 10 likely selected the wrong power domains to shut down.

- [ ] **Step 6: Confirm resume-in-place across sleep (the assumption from spec §4/§6)**

Add a temporary GPIO toggle immediately after `platform_enter_low_power_sleep()` returns in `vault_core_step()` (or observe via debugger if SWD is still accessible at this stage), and confirm execution resumes there — not back at `Reset_Handler` — after each wake. If it resets instead, the `vault_context`/`wake_interval_sec` statics need the explicit retained-memory linker section flagged as an open question in spec §4, and Tasks 2/6 need revisiting.

- [ ] **Step 7: Document findings and commit any fixes found during verification**

If any UM10601 cross-check in Step 1 or any hardware behavior in Steps 2-6 required a code change, commit each fix separately with a message describing what the datasheet/hardware actually showed, e.g.:
```bash
git add platform/lpc810/src/platform_lpc810_i2c.c
git commit -m "Fix I2C0 SLVSTATE bit position per UM10601 Table 231"
```

---

## Phase C — STM32U031F8P6 backend (build-only; no hardware yet)

### Task 12: Vendor the STM32CubeU0 HAL package

**Files:**
- Create: `vendor/STM32CubeU0` (git submodule)
- Create: `platform/stm32u031/CMakeLists.txt`
- Create: `platform/stm32u031/src/smoke_test_main.c`

**Interfaces:**
- Produces: the STM32Cube HAL/LL/CMSIS headers for STM32U031, available via `platform/stm32u031/CMakeLists.txt`'s include paths to all later STM32U031 tasks.

- [ ] **Step 1: Add the submodule, pinned to release v1.3.0**

```bash
git submodule add https://github.com/STMicroelectronics/STM32CubeU0.git vendor/STM32CubeU0
cd vendor/STM32CubeU0 && git checkout v1.3.0 && cd ../..
git add vendor/STM32CubeU0 .gitmodules
```

- [ ] **Step 2: Locate the exact include paths and HAL config template needed**

Run:
```bash
find vendor/STM32CubeU0 -iname 'stm32u031xx.h'
find vendor/STM32CubeU0 -iname 'stm32u0xx_hal_conf_template.h'
find vendor/STM32CubeU0 -path '*STM32U0xx_HAL_Driver/Inc*' -maxdepth 6 -type d
```
Expected: each command prints at least one path. Note the three directories (CMSIS device headers, HAL driver includes, and the CMSIS-Core includes typically at `vendor/STM32CubeU0/Drivers/CMSIS/Include`) — Step 3 references them.

- [ ] **Step 3: Create `platform/stm32u031/CMakeLists.txt` with a smoke-test target**

```cmake
set(STM32CUBE_U0_ROOT ${CMAKE_SOURCE_DIR}/vendor/STM32CubeU0)

set(STM32_INCLUDE_DIRS
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Include
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Include
    ${STM32CUBE_U0_ROOT}/Drivers/STM32U0xx_HAL_Driver/Inc
)

add_library(vault_stm32u031_hal_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_stm32u031_hal_check PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vault_stm32u031_hal_check PRIVATE STM32U031xx)
target_compile_options(vault_stm32u031_hal_check PRIVATE -mcpu=cortex-m0plus -mthumb)
```

- [ ] **Step 4: Copy and minimally trim the HAL config template**

The STM32Cube HAL needs a project-specific `stm32u0xx_hal_conf.h` (normally hand-edited from the vendored template to enable only the modules used):

```bash
cp vendor/STM32CubeU0/Drivers/STM32U0xx_HAL_Driver/Inc/stm32u0xx_hal_conf_template.h \
   platform/stm32u031/include/stm32u0xx_hal_conf.h
```

Open `platform/stm32u031/include/stm32u0xx_hal_conf.h` and confirm `HAL_MODULE_ENABLED`, `HAL_GPIO_MODULE_ENABLED`, `HAL_I2C_MODULE_ENABLED`, `HAL_RTC_MODULE_ENABLED`, `HAL_PWR_MODULE_ENABLED`, and `HAL_CORTEX_MODULE_ENABLED` are all uncommented (the template typically ships with everything enabled by default — leave others disabled/commented to keep the build minimal, since only these five HAL modules are used by this project).

- [ ] **Step 5: Write the smoke-test file**

`platform/stm32u031/src/smoke_test_main.c`:
```c
#include "stm32u0xx_hal.h"

/* Referencing these type/macro names (without calling anything) proves
   the vendored HAL headers resolve cleanly for STM32U031xx under
   arm-none-eabi-gcc -mcpu=cortex-m0plus. */
static I2C_HandleTypeDef s_i2c_handle_check;
static RTC_HandleTypeDef s_rtc_handle_check;

int stm32u031_hal_check_reference(void) {
    return (int)(HAL_GetTick() + s_i2c_handle_check.State + s_rtc_handle_check.State);
}
```

- [ ] **Step 6: Build the smoke-test target and verify it compiles**

Run:
```bash
mkdir -p build/stm32u031 && cd build/stm32u031
cmake ../.. -DVAULT_TARGET=stm32u031
cmake --build . --target vault_stm32u031_hal_check
```
Expected: compiles with no errors. If it fails on a missing header, re-run the `find` commands from Step 2 — ST occasionally reorganizes Cube package directory layouts between releases, and the exact subpath may differ slightly from what's assumed in `STM32_INCLUDE_DIRS`.

- [ ] **Step 7: Commit**

```bash
git add vendor/STM32CubeU0 .gitmodules platform/stm32u031/CMakeLists.txt platform/stm32u031/include/stm32u0xx_hal_conf.h platform/stm32u031/src/smoke_test_main.c
git commit -m "Vendor STM32CubeU0 HAL and verify it builds for STM32U031xx"
```

---

### Task 13: STM32U031F8 linker script, startup file, and CMake target

**Files:**
- Create: `platform/stm32u031/linker/stm32u031f8.ld`
- Modify: `platform/stm32u031/CMakeLists.txt`
- Create: `platform/stm32u031/src/main.c` (placeholder)

**Interfaces:**
- Produces: the `vault_stm32u031` executable CMake target that Tasks 14 add sources to. Startup code and the vector table come from the vendored `startup_stm32u031xx.s` (part of STM32CubeU0's CMSIS device package) rather than being hand-written, unlike the LPC810 backend — ST ships an assembly startup file per device as part of the Cube package, so writing our own would duplicate a vendor-maintained, already-correct file.

- [ ] **Step 1: Locate the vendored startup file**

Run:
```bash
find vendor/STM32CubeU0 -iname 'startup_stm32u031*.s'
```
Expected: prints a path such as `vendor/STM32CubeU0/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/gcc/startup_stm32u031xx.s`. Note the exact path for Step 3.

- [ ] **Step 2: Create the linker script for the STM32U031F8's 64 KB flash / 12 KB SRAM**

`platform/stm32u031/linker/stm32u031f8.ld`:
```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 12K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + 1024;
        . = ALIGN(8);
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

- [ ] **Step 3: Add the executable target, using the vendored startup assembly file from Step 1**

Append to `platform/stm32u031/CMakeLists.txt` (substitute the exact path found in Step 1 if it differs):
```cmake
add_executable(vault_stm32u031
    ${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/gcc/startup_stm32u031xx.s
    src/main.c
)

target_include_directories(vault_stm32u031 PRIVATE
    ${STM32_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(vault_stm32u031 PRIVATE STM32U031xx)
target_compile_options(vault_stm32u031 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_stm32u031 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32u031f8.ld
    -Wl,-Map=vault_stm32u031.map
)

add_custom_command(TARGET vault_stm32u031 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_stm32u031>
)
```

- [ ] **Step 4: Create a placeholder `main.c`**

`platform/stm32u031/src/main.c`:
```c
int main(void) {
    while (1) { }
}
```

- [ ] **Step 5: Build and verify it links**

Run:
```bash
cd build/stm32u031 && cmake --build . --target vault_stm32u031
```
Expected: links successfully; `arm-none-eabi-size` shows usage far under the 64 KB flash / 12 KB SRAM budget for this near-empty program. If the vendored startup `.s` file references symbols (e.g. `SystemInit`) not yet defined, also add `${STM32CUBE_U0_ROOT}/Drivers/CMSIS/Device/ST/STM32U0xx/Source/Templates/system_stm32u0xx.c` to the executable's sources — `find vendor/STM32CubeU0 -iname 'system_stm32u0xx.c'` locates it.

- [ ] **Step 6: Commit**

```bash
git add platform/stm32u031/linker platform/stm32u031/src/main.c platform/stm32u031/CMakeLists.txt
git commit -m "Add STM32U031F8 linker script and executable target"
```

---

### Task 14: STM32U031F8P6 platform backend (HAL-based)

**Files:**
- Create: `platform/stm32u031/src/platform_stm32u031.c`
- Modify: `platform/stm32u031/CMakeLists.txt`, `platform/stm32u031/src/main.c`

**Interfaces:**
- Consumes: STM32Cube HAL (`stm32u0xx_hal.h` and friends, Task 12), `vault_i2c_registers_on_*` hooks (Task 2), `vault_core.h` (Task 4).
- Produces: all eight `platform.h` functions for this backend, plus `HAL_I2C_SlaveRxCpltCallback`/`HAL_I2C_SlaveTxCpltCallback`/`HAL_I2C_ListenCpltCallback` (ST HAL's I2C slave event callbacks, which route into `vault_i2c_registers_on_*`), and `I2C1_IRQHandler`.

Pin/peripheral assignment for this task (fixed for this backend, consistent with the pin budget the STM32U031F8P6 was selected for in the analysis report): `GPIOA` pin 0 drives the main rail; `I2C1` on its default AF pins provides the slave bus; `RTC` provides the wakeup timer, configured to allow Stop 2 entry per the analysis report's own example code. **Verify against your actual schematic before flashing once hardware exists** — no board exists yet to confirm these choices against, so treat them as the same kind of placeholder the LPC810 backend's pin assignments are.

- [ ] **Step 1: Implement GPIO (rail enable, bus isolation) and `platform_init`**

`platform/stm32u031/src/platform_stm32u031.c` (full file, built up across this task's steps — start with this portion):
```c
#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "stm32u0xx_hal.h"

#define MAIN_RAIL_EN_PORT GPIOA
#define MAIN_RAIL_EN_PIN  GPIO_PIN_0
#define I2C_SDA_PORT      GPIOA
#define I2C_SDA_PIN       GPIO_PIN_10
#define I2C_SCL_PORT      GPIOA
#define I2C_SCL_PIN       GPIO_PIN_9

static I2C_HandleTypeDef s_i2c_handle;
static RTC_HandleTypeDef s_rtc_handle;

static void gpio_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = MAIN_RAIL_EN_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MAIN_RAIL_EN_PORT, &gpio_init);
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN, GPIO_PIN_RESET);
}

void platform_init(void) {
    HAL_Init();
    gpio_init();
}

void platform_main_rail_enable(bool on) {
    HAL_GPIO_WritePin(MAIN_RAIL_EN_PORT, MAIN_RAIL_EN_PIN,
                       on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void platform_bus_isolate(void) {
    /* Analog, no pull -- the STM32 equivalent of the LPC810 isolation
       step in platform_lpc810_gpio.c, and the exact mitigation the
       design spec (section 5 of the original MCU analysis report)
       calls for to prevent parasitic back-powering through ESD diodes. */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}
```

- [ ] **Step 2: Implement the RTC-based wakeup timer**

Append to the same file:
```c
static void rtc_init(void) {
    __HAL_RCC_RTC_ENABLE();
    s_rtc_handle.Instance = RTC;
    s_rtc_handle.Init.HourFormat = RTC_HOURFORMAT_24;
    s_rtc_handle.Init.AsynchPrediv = 127;
    s_rtc_handle.Init.SynchPrediv = 255;
    s_rtc_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&s_rtc_handle);
}

void platform_wakeup_timer_arm(uint32_t seconds) {
    /* HAL_RTCEx_SetWakeUpTimer's tick source and the arithmetic to turn
       `seconds` into a wakeup-counter reload value depend on the RTC
       clock source (LSE vs LSI) chosen for the real board -- this uses
       the RTCCLK/16 wakeup clock (WUCKSEL_RTCCLK_DIV16), which needs the
       RTC clock frequency confirmed against the STM32U031 reference
       manual's RTC chapter before the `seconds`-to-ticks arithmetic
       below can be trusted. Placeholder: assumes a 1 Hz effective tick,
       i.e. reload value == seconds, which is only true for specific
       clock configurations -- verify before flashing once hardware
       exists. */
    HAL_RTCEx_DeactivateWakeUpTimer(&s_rtc_handle);
    HAL_RTCEx_SetWakeUpTimer_IT(&s_rtc_handle, seconds, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
}

void platform_wakeup_timer_clear(void) {
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&s_rtc_handle, RTC_FLAG_WUTF);
}
```

- [ ] **Step 3: Implement the I2C1 slave driver using HAL's interrupt-driven listen mode**

Append to the same file:
```c
static void i2c_pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = I2C_SDA_PIN | I2C_SCL_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Alternate = GPIO_AF6_I2C1; /* verify AF number against the
                                            STM32U031 datasheet's
                                            alternate-function table for
                                            whichever pins the real
                                            schematic actually uses */
    HAL_GPIO_Init(I2C_SDA_PORT, &gpio_init);
}

void platform_i2c_slave_init(uint8_t addr) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    i2c_pins_init();

    s_i2c_handle.Instance = I2C1;
    s_i2c_handle.Init.Timing = 0x00303D5B; /* 100 kHz at an assumed 16 MHz
                                               I2C clock -- verify against
                                               CubeMX's timing calculator
                                               for the real clock config
                                               before flashing */
    s_i2c_handle.Init.OwnAddress1 = (uint32_t)(addr << 1);
    s_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c_handle.Init.OwnAddress2 = 0;
    s_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&s_i2c_handle);

    HAL_NVIC_SetPriority(I2C1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C1_IRQn);

    HAL_I2C_EnableListen_IT(&s_i2c_handle);
}

void platform_i2c_slave_deinit(void) {
    HAL_NVIC_DisableIRQ(I2C1_IRQn);
    HAL_I2C_DisableListen_IT(&s_i2c_handle);
    HAL_I2C_DeInit(&s_i2c_handle);
    __HAL_RCC_I2C1_CLK_DISABLE();
}

void I2C1_IRQHandler(void) {
    HAL_I2C_EV_IRQHandler(&s_i2c_handle);
}

static uint8_t s_rx_byte;
static uint8_t s_tx_byte;

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t direction, uint16_t addr_match_code) {
    (void)addr_match_code;
    if (direction == I2C_DIRECTION_TRANSMIT) {
        HAL_I2C_Slave_Sequential_Receive_IT(hi2c, &s_rx_byte, 1, I2C_NEXT_FRAME);
    } else {
        s_tx_byte = vault_i2c_registers_on_read_request();
        HAL_I2C_Slave_Sequential_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    vault_i2c_registers_on_write_byte(s_rx_byte);
    HAL_I2C_Slave_Sequential_Receive_IT(hi2c, &s_rx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    s_tx_byte = vault_i2c_registers_on_read_request();
    HAL_I2C_Slave_Sequential_Transmit_IT(hi2c, &s_tx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    vault_i2c_registers_on_stop();
    HAL_I2C_EnableListen_IT(hi2c);
}
```

This uses HAL's sequential slave receive/transmit in one-byte frames precisely so each byte routes through the same `vault_i2c_registers_on_write_byte`/`on_read_request` hooks the LPC810 backend and the host_mock use — `core/` never has to know which HAL called it.

- [ ] **Step 4: Implement Stop 2 sleep entry**

Append to the same file:
```c
void platform_enter_low_power_sleep(void) {
    /* Matches the analysis report's own example loop: disable I2C
       (already done via platform_i2c_slave_deinit() before this is
       called), suspend SysTick so it can't wake the core prematurely,
       enter Stop 2, then resume SysTick and reconfigure the system
       clock on return -- HAL_PWREx_EnterSTOP2Mode() blocks until an
       enabled wakeup source (the RTC wakeup timer, armed by
       platform_wakeup_timer_arm() before this is called) fires, and
       returns with CPU registers and SRAM intact, consistent with
       vault_core's resume-in-place assumption (spec section 6). */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
    HAL_ResumeTick();
    SystemClock_Config();
}
```

`SystemClock_Config()` doesn't exist yet — add a minimal one to `platform/stm32u031/src/main.c` in the next step (real clock tree setup is deferred to hardware bring-up; for a build-only phase, a stub that just calls `HAL_RCC_OscConfig`/`HAL_RCC_ClockConfig` with a plausible MSI-based configuration is enough to make this compile and link).

- [ ] **Step 5: Add `SystemClock_Config()` and finish wiring `main()`**

Replace `platform/stm32u031/src/main.c` entirely with:
```c
#include "vault/vault_core.h"
#include "vault/platform.h"
#include "stm32u0xx_hal.h"

void SystemClock_Config(void) {
    /* Placeholder MSI-based configuration -- revisit once real hardware
       exists to pick the actual clock tree (e.g. whether LSE is fitted
       for RTC accuracy, matching the STM32U031F8P6's 16-pin budget
       advantage over the LPC810 called out in the analysis report). */
    RCC_OscInitTypeDef osc_init = {0};
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc_init.MSIState = RCC_MSI_ON;
    osc_init.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc_init.MSIClockRange = RCC_MSIRANGE_6; /* ~4 MHz -- verify against
                                                 the actual clock budget
                                                 once hardware exists */
    HAL_RCC_OscConfig(&osc_init);

    RCC_ClkInitTypeDef clk_init = {0};
    clk_init.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_0);
}

int main(void) {
    platform_init();
    vault_core_init();
    for (;;) {
        vault_core_step();
    }
}
```

- [ ] **Step 6: Add the source file to the build and link `vault_core`**

In `platform/stm32u031/CMakeLists.txt`, add `src/platform_stm32u031.c` to `vault_stm32u031`'s sources, and add:
```cmake
target_link_libraries(vault_stm32u031 PRIVATE vault_core)
```

- [ ] **Step 7: Build and verify it compiles and links**

Run:
```bash
cd build/stm32u031 && cmake --build . --target vault_stm32u031
```
Expected: builds successfully. This is a build-time correctness check only, per spec §7/§8 — no hardware exists yet to flash it onto. Resolve any HAL type/macro name mismatches (STM32Cube HAL API details do shift slightly between package versions) by grepping the vendored `stm32u0xx_hal_i2c.h`/`stm32u0xx_hal_rtc.h`/`stm32u0xx_hal_pwr_ex.h` for the exact function/enum names if the compiler reports them undefined.

- [ ] **Step 8: Commit**

```bash
git add platform/stm32u031/src/platform_stm32u031.c platform/stm32u031/src/main.c platform/stm32u031/CMakeLists.txt
git commit -m "Implement STM32U031F8P6 platform backend against STM32Cube HAL"
```

---

### Task 15: Top-level documentation tying the three targets together

**Files:**
- Create: `README.md`

**Interfaces:** none — documentation only.

- [ ] **Step 1: Write the top-level README**

`README.md`:
```markdown
# LoRaWAN Wakeup Manager

Firmware for the "Data Vault" auxiliary MCU described in
`docs/MCU_Analysis_Report.md` and `docs/superpowers/specs/2026-08-08-lorawan-wakeup-manager-design.md`.

## Building

This project uses CMake with three targets, selected via `-DVAULT_TARGET`:

### Host (unit tests, no hardware required)

\`\`\`bash
git submodule update --init --recursive
mkdir -p build/host && cd build/host
cmake ../.. -DVAULT_TARGET=host
cmake --build .
ctest --output-on-failure
\`\`\`

### LPC810 (real hardware, requires arm-none-eabi-gcc)

\`\`\`bash
mkdir -p build/lpc810 && cd build/lpc810
cmake ../.. -DVAULT_TARGET=lpc810
cmake --build .
arm-none-eabi-objcopy -O binary vault_lpc810 vault_lpc810.bin
# flash vault_lpc810.bin via your ISP/SWD programmer
\`\`\`

Before flashing to real hardware, read the verification checklist at the
top of "Phase B" in
`docs/superpowers/plans/2026-08-08-lorawan-wakeup-manager.md` — several
register values in the LPC810 backend are marked as needing cross-check
against the NXP UM10601 reference manual.

### STM32U031F8P6 (build-only until hardware arrives)

\`\`\`bash
mkdir -p build/stm32u031 && cd build/stm32u031
cmake ../.. -DVAULT_TARGET=stm32u031
cmake --build .
\`\`\`

This target compiles and links against the vendored STM32Cube HAL but has
not been flashed or validated on real silicon yet — see the design spec's
"out of scope" section.

## Adding a new STM32 family backend later

1. `git submodule add https://github.com/STMicroelectronics/STM32Cube<Family>.git vendor/STM32Cube<Family>`
2. Add `platform/<family>/` implementing every function in `core/include/vault/platform.h`.
3. Add the target to the top-level `CMakeLists.txt`'s `VAULT_TARGET` dispatch.

`core/` never changes for this.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Add top-level README with build instructions for all three targets"
```

---

## Self-Review Notes

- **Spec coverage:** §1/§2 (layering, compile-time backend selection) → Tasks 1, 5-14. §3 (HAL contract, byte-level I2C hooks) → Tasks 1, 2. §4 (context storage, sizes, retention verification) → Tasks 2 (sizes via `VAULT_CONTEXT_SIZE`), 11 Step 6 (retention verification). §5 (register protocol, including the fixed-address amendment) → Task 2. §6 (state machine, scheduling ownership, persistence, first-boot) → Task 4. §7 (build/test, vendoring) → Tasks 1, 3, 5, 12. §8 (out of scope) → respected: no real LoRaWAN stack anywhere, STM32 hardware bring-up explicitly deferred (Task 14 Step 7 is build-only), no `platform/stm32_common/` introduced, no wake-interval flash persistence, no event-triggered wake, no LPC810 external crystal.
- **Type consistency:** `platform.h` function signatures (Task 1) are used identically by `vault_core.c` (Task 4), `platform_host_mock.c` (Task 3), `platform_lpc810_*.c` (Tasks 7-10), and `platform_stm32u031.c` (Task 14) — verified no drift in parameter types (`bool`, `uint8_t`, `uint32_t`) across all five call sites while writing this plan.
- **Known residual risk, called out rather than hidden:** Phase B's LPC810 register-level code (Tasks 7-9) and the `PDRUNCFG` value in Task 10 are written against standard LPC81x CMSIS naming and documented peripheral behavior, but several exact bit positions are flagged inline as needing verification against the real UM10601 manual — Task 11 exists specifically to close that gap on real hardware before this is considered done, not to paper over it.
