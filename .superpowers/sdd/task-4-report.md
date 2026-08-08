# Task 4 Report — `vault_core` state machine (TDD)

## Summary

Implemented `vault_core` exactly per the brief: created `core/include/vault/vault_core.h`,
wrote `tests/test_vault_core.c`, wired it into `tests/CMakeLists.txt`, verified the tests
fail to link (Task 2's empty placeholder), replaced `core/src/vault_core.c` with the real
implementation, verified tests pass, and committed.

No deviations from the brief's verbatim code were needed — all consumed interfaces
(`platform.h`, `vault_i2c_registers.h`, `host_mock_test_api.h`) already matched what the
brief's test/implementation code expected (enum names, register defines, function
signatures), so the code was used exactly as given.

## Steps performed

1. Created `core/include/vault/vault_core.h` with the `vault_core_init()` /
   `vault_core_step()` declarations, verbatim from the brief.
2. Created `tests/test_vault_core.c`, verbatim from the brief (three tests: ordering of
   the WAKE_MAIN -> BUS_ISOLATION -> ARM_SLEEP call sequence, default wake interval used
   on first cycle, main-MCU-configured interval used on the next cycle). Test index usage
   into `host_mock_call_at()` stays within bounds guarded by `host_mock_call_count()`, per
   the known host_mock gotcha from Task 3's review — no changes needed since the brief's
   test code already guards it correctly.
3. Appended the `test_vault_core` executable/target/test registration to
   `tests/CMakeLists.txt`, verbatim from the brief.
4. Ran `cmake ../.. -DVAULT_TARGET=host && cmake --build .` to confirm the tests fail to
   build for the right reason (see "Verify it fails" output below).
5. Replaced `core/src/vault_core.c` entirely (previously just a placeholder comment left
   by Task 2) with the real implementation, verbatim from the brief.
6. Rebuilt and ran `ctest --output-on-failure` — both suites pass (see "Verify it passes"
   output below).
7. Committed.

## Verify it fails (Step 4)

Command: `cd build/host && cmake ../.. -DVAULT_TARGET=host && cmake --build .`

```
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/smiff/git/lorawan-wakeup-manager/build/host
[ 33%] Built target vault_core
[ 55%] Built target vault_platform_host_mock
[ 77%] Built target test_vault_i2c_registers
[ 88%] Building C object tests/CMakeFiles/test_vault_core.dir/test_vault_core.c.o
[100%] Linking C executable test_vault_core
ld: warning: ignoring duplicate libraries: '../core/libvault_core.a'
Undefined symbols for architecture arm64:
  "_vault_core_init", referenced from:
      _test_first_cycle_calls_in_order in test_vault_core.c.o
      _test_default_interval_used_on_first_cycle in test_vault_core.c.o
      _test_main_mcu_configured_interval_used_next_cycle in test_vault_core.c.o
  "_vault_core_step", referenced from:
      _test_first_cycle_calls_in_order in test_vault_core.c.o
      _test_default_interval_used_on_first_cycle in test_vault_core.c.o
      _test_main_mcu_configured_interval_used_next_cycle in test_vault_core.c.o
      _test_main_mcu_configured_interval_used_next_cycle in test_vault_core.c.o
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
gmake[2]: *** [tests/CMakeFiles/test_vault_core.dir/build.make:103: tests/test_vault_core] Error 1
gmake[1]: *** [CMakeFiles/Makefile2:252: tests/CMakeFiles/test_vault_core.dir/all] Error 2
gmake: *** [Makefile:101: all] Error 2
```

This confirms the expected failure mode: `vault_core_init`/`vault_core_step` undefined
(Task 2's placeholder `vault_core.c` had no implementation), not a compile error or a
different link issue.

## Verify it passes (Step 6)

Command: `cd build/host && cmake --build . && ctest --output-on-failure`

```
[ 11%] Building C object core/CMakeFiles/vault_core.dir/src/vault_core.c.o
[ 22%] Linking C static library libvault_core.a
[ 33%] Built target vault_core
[ 55%] Built target vault_platform_host_mock
[ 66%] Linking C executable test_vault_i2c_registers
[ 77%] Built target test_vault_i2c_registers
[ 88%] Linking C executable test_vault_core
ld: warning: ignoring duplicate libraries: '../core/libvault_core.a'
[100%] Built target test_vault_core
Test project /Users/smiff/git/lorawan-wakeup-manager/build/host
    Start 1: vault_i2c_registers
1/2 Test #1: vault_i2c_registers ..............   Passed    0.45 sec
    Start 2: vault_core
2/2 Test #2: vault_core .......................   Passed    0.31 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   0.77 sec
```

Both suites (`vault_i2c_registers` from Task 2, `vault_core` new in this task) pass.

## Files changed

- Created: `core/include/vault/vault_core.h`
- Modified: `core/src/vault_core.c` (replaced Task 2's placeholder with the full
  implementation)
- Created: `tests/test_vault_core.c`
- Modified: `tests/CMakeLists.txt`

## Commit

```
git add core/ tests/
git commit -m "Implement vault_core wake/sleep state machine"
```

(Commit hash recorded in the final status message to the orchestrator.)

## Notes / concerns

- The pre-existing `ld: warning: ignoring duplicate libraries: '../core/libvault_core.a'`
  linker warning is unrelated to this task (also present in the "verify it fails" build
  before any vault_core.c change) and does not affect test results.
- No deviations from the brief. All constraints (register addresses fixed, little-endian
  multi-byte fields, `VAULT_I2C_ADDR` 0x42, `VAULT_DEFAULT_WAKE_INTERVAL_SEC` 60, no vendor
  SDK headers in `core/`) were already satisfied by the existing Task 1-3 code that this
  task builds on; `vault_core.c` itself introduces no new constants or vendor
  dependencies.

## Correction (post-review)

The note at line 116 stating the linker warning was "pre-existing and unrelated to this
task" was incorrect. The warning was in fact introduced by this task's CMakeLists.txt
addition (the explicit `vault_core vault_platform_host_mock` dual link at line 6 of
`tests/CMakeLists.txt`). Since `vault_platform_host_mock` already links `vault_core`
transitively via its own CMakeLists.txt PUBLIC dependency, the explicit `vault_core` link
was redundant. This redundancy has now been fixed by removing the duplicate `vault_core`
link, eliminating the warning without affecting test results.
