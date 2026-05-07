# kernel_blob — embeds the assembled MS-DOS 1.0 kernel binary.
#
# build/kernel.bin is produced by ../../../asm/build_kernel.sh from
# Tim Paterson's 86DOS.ASM source listings. We embed it as a binary
# blob so the firmware contains the entire DOS kernel as part of its
# .rodata, accessible at runtime via:
#
#   extern const uint8_t kernel_bin_start[] asm("_binary_kernel_bin_start");
#   extern const uint8_t kernel_bin_end[]   asm("_binary_kernel_bin_end");
#
# At boot, espdos copies kernel_bin_start[..end] into the 8086 emulator's
# memory buffer at KERNEL_SEG:0000 and sets CS:IP to that.

idf_component_register(
    SRCS "kernel_blob.c"
    INCLUDE_DIRS "include"
    EMBED_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/kernel.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/hello.bin"
        # Mandel-loader variant of the boot stub. Same kernel, but the
        # embedded loader stub hard-codes MANDEL.COM's sector instead of
        # HELLO.COM's. The firmware (espdos.c) defaults to using this
        # variant so the on-hardware demo prints the Mandelbrot.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub_mandel.bin"
        # Count-loader variant of the boot stub. Loads COUNT.COM
        # (sector 13). Selected via -DESPDOS_LOADER_COUNT=1 at build.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub_count.bin"
        # Shell-loader variant. Loads SHELL.COM (sector 14), which
        # then dispatches to HELLO/COUNT/MANDEL/JULIA based on a keystroke.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub_shell.bin"
        # Julia-loader variant. Loads JULIA.COM (3 sectors starting at
        # sector 15). Selected via -DESPDOS_LOADER_JULIA=1.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub_julia.bin"
        # Life-loader variant. Loads LIFE.COM (2 sectors starting at
        # sector 18). Selected via -DESPDOS_LOADER_LIFE=1.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/bootstub_life.bin"
)
