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
