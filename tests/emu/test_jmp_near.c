/*
 * test_jmp_near — does a single JMP near (E9) update IP correctly?
 *
 * This is the simplest possible smoke test for the 8086tiny instruction
 * decoder. It catches the "IP frozen" symptom we saw in QEMU runs of
 * the kernel, deterministically and in <1s.
 *
 * Bytecode at CS:IP = 0x0100:0000:
 *     E9 03 00     JMP +0x0003
 *     90           NOP   (would be at IP=0x0006 after the jump)
 *
 * Expected after one step:
 *     CS unchanged at 0x0100
 *     IP = 0x0006   (= 3 bytes for the JMP instruction + 0x0003 offset)
 */

#include <stdio.h>

#include "test_helpers.h"

int main(void) {
    int fails = 0;

    if (t_load_bios() != 0) {
        fprintf(stderr, "FATAL: could not load 8086tiny BIOS\n");
        return 2;
    }

    /* Tiny program: JMP +3 ; NOP. */
    static const unsigned char prog[] = { 0xE9, 0x03, 0x00, 0x90 };
    t_load(0x0100, 0x0000, prog, sizeof prog);

    emu_init_state();
    emu_set_cs_ip(0x0100, 0x0000);
    t_dump_state("before");

    int still_running = emu_run_n(1);
    t_dump_state("after-1");

    fails += T_EXPECT(still_running == 1);
    fails += T_EXPECT_EQ(emu_get_cs(), 0x0100);
    fails += T_EXPECT_EQ(emu_get_ip(), 0x0006);

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
