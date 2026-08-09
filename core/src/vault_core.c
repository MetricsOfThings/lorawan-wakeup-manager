#include "vault/vault_core.h"
#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "vault/vault_log.h"

void vault_core_init(void) {
    vault_state_set_wake_interval_sec(VAULT_DEFAULT_WAKE_INTERVAL_SEC);
    vault_log("vault_core: init\n");
}

void vault_core_step(void) {
    /* WAKE_MAIN */
    vault_log("vault_core: wake_main enter\n");
    platform_main_rail_enable(true);
    vault_i2c_registers_reset_for_cycle();
    platform_i2c_slave_init(VAULT_I2C_ADDR);

    while (!vault_i2c_registers_done_requested()) {
        /* Busy-wait. Real backends service I2C via an interrupt handler
           that calls vault_i2c_registers_on_*() concurrently with this
           loop; see platform/lpc810 and platform/stm32u031. Do not add
           vault_log() calls to that ISR-driven path -- it blocks on a
           slow UART transmit, which would corrupt I2C timing. This is
           the only place safe to log what happened, after the ISR-driven
           exchange has already finished. */
    }

    vault_log_u32("vault_core: context_valid=", vault_state_context_valid() ? 1u : 0u);

    /* BUS_ISOLATION */
    platform_i2c_slave_deinit();
    platform_bus_isolate();
    platform_main_rail_enable(false);
    vault_log("vault_core: bus_isolation done\n");

    /* ARM_SLEEP */
    uint32_t interval = vault_state_wake_interval_sec();
    vault_log_u32("vault_core: arm_sleep interval_sec=", interval);
    platform_wakeup_timer_arm(interval);
    vault_log("vault_core: sleep\n");
    platform_enter_low_power_sleep();
    vault_log("vault_core: wake\n");
}
