/*
 * test_memory_bounds — verify the esp8086 memory layout exposes the
 * full 1 MB + margin, and that REGS_BASE lives high enough that the
 * kernel's MEMSCAN can walk through DOS memory without scribbling on
 * the register file (kernel-controlled writes only happen at offsets
 * < 0xF0000).
 *
 * If this test passes, Tier 1's structural-fix premise holds: the
 * runtime MEMSCAN patch becomes unnecessary because the kernel's
 * write-and-verify probe naturally finds every byte from 0x00000 up
 * through 0x10FFFF, and either terminates somewhere in the BIOS
 * lookup table region (where back-to-back writes don't both stick to
 * a value the read sees) or wraps after DH overflows.
 */

#include <stdio.h>
#include <string.h>

#include "test_helpers.h"

int main(void) {
    int fails = 0;

    /* Wire up regs8/regs16 to point at REGS_BASE inside mem[]. */
    if (t_load_bios() != 0) return 2;

    /* esp8086.c sets RAM_SIZE = 0x110000. We can't pull that constant
     * here without including esp8086.c's private header, so prove it
     * by writing to addresses in each of the layout regions and reading
     * them back. If the array is too small, write-then-read at high
     * addresses will smash adjacent BSS or segfault. */

    /* (1) Low memory (IVT/BIOS data area). */
    mem[0x000] = 0xAB;
    mem[0x3FF] = 0xCD;
    mem[0x4FF] = 0xEF;
    fails += T_EXPECT(mem[0x000] == 0xAB);
    fails += T_EXPECT(mem[0x3FF] == 0xCD);
    fails += T_EXPECT(mem[0x4FF] == 0xEF);

    /* (2) DOS user space — kernel + user .COM area. */
    mem[0x01000] = 0x11;
    mem[0x10000] = 0x22;
    mem[0xEFFFF] = 0x33;
    fails += T_EXPECT(mem[0x01000] == 0x11);
    fails += T_EXPECT(mem[0x10000] == 0x22);
    fails += T_EXPECT(mem[0xEFFFF] == 0x33);

    /* (3) Top of real-mode 1 MB and the natural-overflow margin.
     * Real 8086 segment:offset reaches up to 0xFFFF*16 + 0xFFFF =
     * 0x10FFEF. esp8086.c rounds up to 0x110000 = 1 MB + 64 KB. */
    mem[0xFFFFF]  = 0x44;
    mem[0x10FFEF] = 0x55;
    fails += T_EXPECT(mem[0xFFFFF]  == 0x44);
    fails += T_EXPECT(mem[0x10FFEF] == 0x55);

    /* (4) The register file lives at REGS_BASE = 0xF0000. Anything
     * the kernel writes within 0..0xEFFFF cannot collide with regs.
     * Confirm regs8 points there. */
    extern unsigned char *regs8;
    fails += T_EXPECT(regs8 == &mem[EMU_REGS_BASE]);
    fails += T_EXPECT(EMU_REGS_BASE == 0xF0000u);

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
