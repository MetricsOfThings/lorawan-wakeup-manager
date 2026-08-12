/* Placeholder main() for this task: platform_init()/vault_core_init()/
 * vault_core_step() are not wired in yet (platform_init() has no
 * efm32g210 implementation until Task 3 onward, and vault_core is not
 * yet linked into this target). Matches the precedent set by both other
 * backends at this exact stage -- LPC810's Task 6 main.c and STM32U031's
 * "Add STM32U031F8 linker script and executable target" commit (d262d73)
 * both used a plain while(1) loop here, wiring the real call sequence in
 * a later task (LPC810 Task 10, STM32U031 Task 14) once vault_core and
 * the backend's platform_*() functions actually exist to link against.
 */
int main(void) {
    while (1) { }
}
