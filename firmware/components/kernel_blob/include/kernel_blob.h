#pragma once

#include <stddef.h>
#include <stdint.h>

/* Embedded MS-DOS 1.0 kernel — assembled by ../asm/build_kernel.sh from
 * Tim Paterson's 86DOS.ASM source. Lives in firmware .rodata. */
extern const uint8_t  kernel_bin_start[] asm("_binary_kernel_bin_start");
extern const uint8_t  kernel_bin_end[]   asm("_binary_kernel_bin_end");

/* Convenience: byte length of the kernel image. */
size_t kernel_blob_size(void);
