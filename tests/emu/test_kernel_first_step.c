/*
 * test_kernel_first_step — load our actual kernel.bin and run ONE
 * instruction. The kernel starts with `JMP DOSINIT` (opcode 0xE9
 * followed by a little-endian 16-bit signed offset). We don't
 * hardcode the offset; we read it from the file so the test stays
 * green if NASM ever picks a different DOSINIT placement.
 *
 * In the firmware this same setup leaves IP frozen at 0. If this
 * test passes here, the bug is in the firmware's emu_load wiring
 * or Xtensa-specific behavior, NOT in the emulator or kernel.
 */

#include <stdio.h>
#include <stdlib.h>

#include "test_helpers.h"

int main(void) {
    int fails = 0;

    if (t_load_bios() != 0) return 2;

    FILE *f = fopen("../../build/kernel.bin", "rb");
    if (!f) {
        perror("kernel.bin");
        return 2;
    }
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    /* Sanity-check the entry sequence and derive the expected post-JMP
     * IP from the on-disk bytes rather than a hardcoded value. */
    fails += T_EXPECT_EQ(buf[0], 0xE9);  /* JMP near */
    int16_t  jmp_off  = (int16_t)(buf[1] | ((uint16_t)buf[2] << 8));
    uint16_t expected_ip = (uint16_t)(3 + jmp_off);
    printf("kernel.bin: %zu bytes, opcode=%02x offset=0x%04x (signed %d) "
           "→ expected IP after JMP = 0x%04x\n",
           n, buf[0], (uint16_t)jmp_off, jmp_off, expected_ip);

    /* Load at offset 0x100 to match `org 0x100` in 86DOS.ASM. */
    t_load(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET, buf, n);

    emu_init_state();
    emu_set_cs_ip(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET);
    t_dump_state("before");

    int still_running = emu_run_n(1);
    t_dump_state("after-jmp");

    /* Expected IP after the first JMP: org_offset (0x100) + 3-byte
     * JMP encoding + signed 16-bit offset from buf[1..2]. */
    uint16_t expected_after_jmp = (uint16_t)(EMU_KERNEL_OFFSET + 3 + jmp_off);

    fails += T_EXPECT(still_running == 1);
    fails += T_EXPECT_EQ(emu_get_cs(), EMU_KERNEL_SEG);
    fails += T_EXPECT_EQ(emu_get_ip(), expected_after_jmp);

    /* DOSINIT starts with CLI (0xFA). */
    uint8_t *m = mem;
    uint32_t phys = ((uint32_t)EMU_KERNEL_SEG << 4) + emu_get_ip();
    fails += T_EXPECT_EQ(m[phys], 0xFA);

    /* Run more steps and check that IP advances each time. If our
     * offset fix is real, the kernel should make progress through
     * DOSINIT's instructions. If IP wedges anywhere, something else
     * is broken. */
    uint16_t prev_ip = emu_get_ip();
    int wedge_count = 0;
    for (int n = 0; n < 200; n++) {
        if (!emu_run_n(1)) break;
        uint16_t ip = emu_get_ip();
        if (ip == prev_ip) wedge_count++;
        else                wedge_count = 0;
        if (wedge_count > 3) {
            printf("  IP wedged at %04x after %d total steps\n", ip, n+2);
            break;
        }
        prev_ip = ip;
    }
    t_dump_state("after-200-more");
    fails += T_EXPECT(wedge_count <= 3);

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
