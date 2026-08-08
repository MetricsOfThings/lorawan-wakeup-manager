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
