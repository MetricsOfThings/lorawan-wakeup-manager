/* STM32C011J6M6: 32 KB flash, 6 KB SRAM. Confirmed against
 * vendor/STM32CubeC0/Drivers/CMSIS/Device/ST/STM32C0xx/Include/stm32c011xx.h:
 * FLASH_SIZE_DEFAULT == 0x8000 (32 KB), SRAM_SIZE_MAX == 0x1800 (6 KB). */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 6K
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
        . = ALIGN(4);
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

    /* 512 bytes, not the STM32U031 backend's 1024 -- half of this
     * part's entire 6 KB SRAM is already committed to
     * VAULT_CONTEXT_SIZE's 320 bytes plus its own overhead, so the
     * stack reservation is kept proportionally smaller. Revisit once
     * the real call depth (HAL_I2C listen-mode callbacks, HAL_PWREx
     * Stop-mode entry) is known. */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + 512;
        . = ALIGN(8);
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
