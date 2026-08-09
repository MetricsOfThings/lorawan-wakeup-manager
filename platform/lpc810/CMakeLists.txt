add_library(vault_lpc810_cmsis_check STATIC EXCLUDE_FROM_ALL
    src/smoke_test_main.c
)

target_include_directories(vault_lpc810_cmsis_check PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
)

target_compile_options(vault_lpc810_cmsis_check PRIVATE -mcpu=cortex-m0plus -mthumb)

add_executable(vault_lpc810
    src/startup_lpc810.c
    src/main.c
    src/platform_lpc810_gpio.c
    src/platform_lpc810_timer.c
    src/platform_lpc810_i2c.c
    src/platform_lpc810_power.c
)

target_include_directories(vault_lpc810 PRIVATE
    vendor
    ${CMAKE_SOURCE_DIR}/vendor/CMSIS_5/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/core/include
)

# platform_lpc810_i2c.c calls vault_i2c_registers_on_*(), defined in
# vault_core; link it in now that the I2C driver needs it (Task 9).
# vault_core's own -mcpu/-mthumb/-Os compile options are set in
# core/CMakeLists.txt, keyed on VAULT_TARGET, rather than duplicated here.
target_link_libraries(vault_lpc810 PRIVATE vault_core)

target_compile_options(vault_lpc810 PRIVATE -mcpu=cortex-m0plus -mthumb -Os)
target_link_options(vault_lpc810 PRIVATE
    -mcpu=cortex-m0plus -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc810.ld
    -Wl,-Map=vault_lpc810.map
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The LPC8xx boot ROM only treats an image as valid application code if
# the vector table's checksum word (offset 0x1C) is correct -- without
# this, some flashing tools (e.g. plain dfu/ISP loaders) silently refuse
# to boot the image, and others (e.g. OpenOCD) patch it at flash time but
# then report a false "verify failed" when comparing against the
# unpatched file on disk. Patching it into our own build output avoids
# both problems for every flashing tool, not just the ones smart enough
# to work around a missing checksum themselves.
add_custom_command(TARGET vault_lpc810 POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vault_lpc810>
    # Extract just the vector table (not the whole image) so it can be
    # patched and written back as a same-size section replacement.
    COMMAND ${CMAKE_OBJCOPY} -O binary --only-section=.isr_vector $<TARGET_FILE:vault_lpc810> $<TARGET_FILE:vault_lpc810>.vectors.bin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/fix_lpc_checksum.py $<TARGET_FILE:vault_lpc810>.vectors.bin
    COMMAND ${CMAKE_OBJCOPY} --update-section .isr_vector=$<TARGET_FILE:vault_lpc810>.vectors.bin $<TARGET_FILE:vault_lpc810>
    # Now produce the flat .bin from the ELF, which already has the
    # corrected checksum baked in.
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:vault_lpc810> $<TARGET_FILE:vault_lpc810>.bin
    COMMENT "Patching LPC8xx boot checksum into vault_lpc810 and vault_lpc810.bin"
)
