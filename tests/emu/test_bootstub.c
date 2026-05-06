/*
 * test_bootstub — load asm/bootstub.asm's output, run it, and verify:
 *   1. the boot stub finishes by JMP-FARing to KERNEL_SEG:KERNEL_OFFSET
 *   2. it wrote our halt routine's address into IVT[20h], [22h], [23h],
 *      [24h], [27h] before doing so
 *   3. the halt routine itself is the right two bytes (HLT; JMP -3)
 *
 * If this passes on host, the firmware boot path works the same way.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_helpers.h"

static uint16_t rd16(uint32_t phys) {
    return (uint16_t)(mem[phys] | ((uint16_t)mem[phys + 1] << 8));
}

int main(void) {
    int fails = 0;

    if (t_load_bios() != 0) return 2;

    FILE *f = fopen("../../build/bootstub.bin", "rb");
    if (!f) { perror("bootstub.bin"); return 2; }
    unsigned char buf[1024];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    printf("bootstub.bin: %zu bytes loaded at %04x:%04x\n",
           n, EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    t_load(EMU_BOOT_SEG, EMU_BOOT_OFFSET, buf, n);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);
    t_dump_state("before");

    /* The stub is ~50 bytes — give it a generous step budget so we
     * comfortably reach the JMP FAR. */
    int still_running = 1;
    for (int i = 0; i < 100; i++) {
        if (!emu_run_n(1)) { still_running = 0; break; }
        /* Stop as soon as CS changes — that means we've taken the
         * JMP FAR out of the boot stub. */
        if (emu_get_cs() != EMU_BOOT_SEG) break;
    }
    t_dump_state("after");

    /* Boot stub must JMP FAR to the kernel entry. */
    fails += T_EXPECT(still_running == 1);
    fails += T_EXPECT_EQ(emu_get_cs(), EMU_KERNEL_SEG);
    fails += T_EXPECT_EQ(emu_get_ip(), EMU_KERNEL_OFFSET);

    /* DS:SI must point at the DPB init table when control reaches
     * the kernel. The kernel's first instruction (LODB at line
     * 3301 of 86DOS.ASM) reads NUMDRV from there. We verify by
     * reading what's at DS:SI from the emulated mem and checking
     * the format: NUMDRV=1, then a DPT pointer that — when followed
     * within the same segment — yields SECSIZ=512, SEC_PER_CLUS=1,
     * etc. matching tools/build_disk.py geometry. */
    extern unsigned char  *regs8;
    extern unsigned short *regs16;
    /* DS index in regs16 is 11 per 8086tiny conventions. */
    uint16_t ds = regs16[11];
    /* SI index in regs16 is 6. */
    uint16_t si = regs16[6];
    uint32_t dpb_phys = ((uint32_t)ds << 4) + si;

    uint8_t  numdrv  = mem[dpb_phys + 0];
    uint16_t dpt_off = rd16(dpb_phys + 1);
    fails += T_EXPECT_EQ(numdrv, 1);
    printf("  DS:SI = %04x:%04x → NUMDRV=%u, DPT_PTR=%04x\n",
           ds, si, numdrv, dpt_off);

    uint32_t dpt_phys = ((uint32_t)ds << 4) + dpt_off;
    uint16_t secsiz   = rd16(dpt_phys + 0);
    uint8_t  secpclus = mem[dpt_phys + 2];
    uint16_t firfat   = rd16(dpt_phys + 3);
    uint8_t  fatcnt   = mem[dpt_phys + 5];
    uint16_t maxent   = rd16(dpt_phys + 6);
    uint16_t dsksiz   = rd16(dpt_phys + 8);
    fails += T_EXPECT_EQ(secsiz,   512);
    fails += T_EXPECT_EQ(secpclus, 1);
    fails += T_EXPECT_EQ(firfat,   1);
    fails += T_EXPECT_EQ(fatcnt,   2);
    fails += T_EXPECT_EQ(maxent,   64);
    fails += T_EXPECT_EQ(dsksiz,   720);
    printf("  DPT[A]: secsiz=%u sec/clus=%u firfat=%u fatcnt=%u "
           "maxent=%u dsksiz=%u (matches tools/build_disk.py)\n",
           secsiz, secpclus, firfat, fatcnt, maxent, dsksiz);

    /* IVT entries 20h, 22h, 23h, 24h, 27h must all point at the same
     * halt label inside the boot stub. We don't hardcode where halt
     * lives — we verify via consistency: all five vectors point to
     * the same (offset, segment) pair, the segment is EMU_BOOT_SEG,
     * and the bytes at that linear address are the HLT/JMP loop. */
    const uint8_t vecs[] = { 0x20, 0x22, 0x23, 0x24, 0x27 };
    uint16_t halt_off = rd16(0x20 * 4);
    uint16_t halt_seg = rd16(0x20 * 4 + 2);

    fails += T_EXPECT_EQ(halt_seg, EMU_BOOT_SEG);
    for (size_t i = 0; i < sizeof vecs; i++) {
        uint16_t off = rd16(vecs[i] * 4);
        uint16_t seg = rd16(vecs[i] * 4 + 2);
        if (off != halt_off || seg != halt_seg) {
            printf("  IVT[%02x] = %04x:%04x but expected %04x:%04x\n",
                   vecs[i], seg, off, halt_seg, halt_off);
            fails++;
        }
    }
    printf("  halt routine installed at %04x:%04x (one source of truth)\n",
           halt_seg, halt_off);

    /* Halt routine bytes: HLT (0xF4), then JMP rel8 -3 (EB FD). */
    uint32_t halt_phys = ((uint32_t)halt_seg << 4) + halt_off;
    fails += T_EXPECT_EQ(mem[halt_phys + 0], 0xF4);
    fails += T_EXPECT_EQ(mem[halt_phys + 1], 0xEB);
    fails += T_EXPECT_EQ(mem[halt_phys + 2], 0xFD);

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
