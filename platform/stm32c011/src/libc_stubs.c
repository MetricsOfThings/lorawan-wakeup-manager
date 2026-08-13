/* Same reason as platform/stm32u031/src/libc_stubs.c: the vendored
 * startup file calls __libc_init_array, which references _init/_fini
 * normally supplied by libc startfiles -- but this project links with
 * -nostartfiles (see cmake/toolchain-arm-none-eabi.cmake). Provide an
 * empty stub so the link succeeds; there are no static constructors to
 * run in this project. */
void _init(void) {}
