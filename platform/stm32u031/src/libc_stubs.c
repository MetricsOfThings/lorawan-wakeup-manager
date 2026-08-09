/* The vendored startup_stm32u031xx.s calls __libc_init_array (to run any
 * C++-style static constructors) before main(). __libc_init_array in turn
 * references _init/_fini, which are normally supplied by the toolchain's
 * crti.o/crtn.o startfiles -- but this project links with -nostartfiles
 * (see cmake/toolchain-arm-none-eabi.cmake) to avoid pulling in libc's
 * _start/exit machinery on a target with no OS. Provide empty stubs so the
 * link succeeds; there are no static constructors to run in this project. */
void _init(void) {}
