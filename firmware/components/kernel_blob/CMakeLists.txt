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
    EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/kernel.bin"
)
