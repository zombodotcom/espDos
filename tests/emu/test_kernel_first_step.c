/*
 * test_kernel_first_step — load our actual kernel.bin and run ONE
 * instruction. The kernel starts with `e9 6d 14` = JMP near +0x146d.
 * After one step, IP should be 3 + 0x146d = 0x1470.
 *
 * In the firmware this same setup leaves IP frozen at 0. If this
 * test passes here, the bug is in the firmware's emu_alloc_mem /
 * emu_load wiring, NOT in the emulator or kernel.
 */

#include <stdio.h>
#include <stdlib.h>

#include "test_helpers.h"

int main(void) {
    int fails = 0;

    if (t_load_bios() != 0) return 2;

    /* Load build/kernel.bin at 0100:0000. */
    FILE *f = fopen("../../build/kernel.bin", "rb");
    if (!f) {
        perror("kernel.bin");
        return 2;
    }
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    printf("loaded kernel.bin: %zu bytes (first 4: %02x %02x %02x %02x)\n",
           n, buf[0], buf[1], buf[2], buf[3]);

    t_load(0x0100, 0x0000, buf, n);

    emu_init_state();
    emu_set_cs_ip(0x0100, 0x0000);
    t_dump_state("before");

    int still_running = emu_run_n(1);
    t_dump_state("after-1-step");

    /* Expected: JMP near offset = (0x146d) → IP = 3 + 0x146d = 0x1470. */
    fails += T_EXPECT(still_running == 1);
    fails += T_EXPECT_EQ(emu_get_cs(), 0x0100);
    fails += T_EXPECT_EQ(emu_get_ip(), 0x1470);

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
