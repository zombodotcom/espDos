/*
 * test_loader — full end-to-end exercise of espDos's transient loader.
 *
 * Boots bootstub → kernel → date prompt → FININIT-RETF → loader →
 * INT 21h OPEN/SEQRD reads HELLO.COM off the disk image → JMP FAR
 * USER_SEG:0x100 → HELLO.COM prints "Hello, World!" via INT 21h AH=09
 * → INT 20h → bootstub `halt`. Asserts:
 *   1. The string "Hello, World!" appears in captured BIOSOUT.
 *   2. The emulator either halts cleanly (CS:IP→0:0) or wedges on
 *      the bootstub `halt` HLT/JMP loop after HELLO terminates.
 *
 * Disk-side BIOSREAD is satisfied from build/disk.img which the
 * build_disk.py invocation places HELLO.COM (built from
 * asm/hellotr.asm) into FAT12 cluster 2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_helpers.h"

#define R_AL  0
#define R_BX  3
#define R_DS  11
#define F_CF  40

extern unsigned char  *regs8;
extern unsigned short *regs16;

#define BIOS_OFF_STAT  0x03
#define BIOS_OFF_IN    0x06
#define BIOS_OFF_OUT   0x09
#define BIOS_OFF_READ  0x15

static char     out_buf[8192];
static size_t   out_len;
static unsigned char *disk_img;
static size_t   disk_size;
static const char *date_input  = "01-01-81\r";
static size_t      date_pos    = 0;

static void out_put(uint8_t c) {
    if (out_len < sizeof(out_buf) - 1) {
        out_buf[out_len++] = (char)c;
        out_buf[out_len]   = 0;
    }
}

static void kernel_bios(unsigned short ip) {
    uint8_t  al = regs8[R_AL];
    uint16_t bx = regs16[R_BX];
    uint16_t cx = regs16[1];
    uint16_t dx = regs16[2];
    uint16_t ds = regs16[R_DS];

    switch (ip) {
    case BIOS_OFF_OUT:
        out_put(al);
        break;
    case BIOS_OFF_IN:
        if (date_input[date_pos]) {
            regs8[R_AL] = (uint8_t)date_input[date_pos++];
        } else {
            regs8[R_AL] = 0x0D;
        }
        break;
    case BIOS_OFF_STAT:
        regs8[R_AL] = 0;
        break;
    case BIOS_OFF_READ: {
        if (al != 0 || !disk_img) { regs8[F_CF] = 1; break; }
        uint32_t off = (uint32_t)dx * 512;
        uint32_t len = (uint32_t)cx * 512;
        if (off + len > disk_size)  { regs8[F_CF] = 1; break; }
        uint32_t bp = ((uint32_t)ds << 4) + bx;
        memcpy(&mem[bp], &disk_img[off], len);
        regs8[F_CF] = 0;
        break;
    }
    default:
        break;
    }
}

static int load_blob(const char *path, uint16_t seg, uint16_t off) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    t_load(seg, off, buf, n);
    return 0;
}

static int load_disk(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END); disk_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    disk_img = malloc(disk_size);
    fread(disk_img, 1, disk_size, f);
    fclose(f);
    return 0;
}

int main(void) {
    int fails = 0;

    if (t_load_bios()                                       != 0) return 2;
    if (load_disk("../../build/disk.img")                   != 0) return 2;
    if (load_blob("../../build/bootstub.bin",
                  EMU_BOOT_SEG, EMU_BOOT_OFFSET)            != 0) return 2;
    if (load_blob("../../build/kernel.bin",
                  EMU_KERNEL_SEG, EMU_KERNEL_OFFSET)        != 0) return 2;

    out_len = 0; out_buf[0] = 0;
    date_pos = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    /* Run with a generous instruction budget. We stop early once we
     * see the transient marker — that's the success criterion. We
     * also stop if the emulator halts (CS:IP→0:0) so we can report
     * cleanly either way. */
    int total = 0;
    const int budget = 5000000;
    int halted = 0;
    /* Phase 1: bulk run small chunks until date input has been
     * fully consumed by BUFIN. */
    while (total < budget) {
        if (date_pos >= 9) break;
        if (!emu_run_n(50)) { total += 50; halted = 1; break; }
        total += 50;
    }
    printf("phase1 (until date consumed): total=%d, date_pos=%zu\n",
           total, date_pos);
    /* Phase 2: bulk run until halt OR until both "Hello, World!" has
     * been printed AND we've stayed in BOOT_SEG (= our halt) for a
     * stable number of iterations. */
    int single_steps = 0;
    int saw_marker_at = -1;
    while (!halted && single_steps < 500000) {
        if (!emu_run_n(1)) { halted = 1; break; }
        single_steps++;
        if (saw_marker_at < 0 && strstr(out_buf, "Hello, World!"))
            saw_marker_at = single_steps;
        /* After the marker shows, give the program another 50000
         * instructions to terminate (HELLO does INT 20h → halt). */
        if (saw_marker_at >= 0 &&
            single_steps - saw_marker_at > 50000) break;
    }
    total += single_steps;
    printf("phase2: %d single steps; marker first seen at +%d\n",
           single_steps, saw_marker_at);

    printf("ran %d instructions, captured %zu bytes:\n", total, out_len);
    /* Echo the captured output with non-printables escaped. */
    for (size_t i = 0; i < out_len; i++) {
        unsigned char c = (unsigned char)out_buf[i];
        if (c == '\r')      printf("\\r");
        else if (c == '\n') printf("\\n\n");
        else if (c < 32 || c >= 127) printf("\\x%02x", c);
        else                putchar(c);
    }
    printf("\n----\n");
    printf("end CS:IP = %04x:%04x  halted=%d\n",
           emu_get_cs(), emu_get_ip(), halted);

    fails += T_EXPECT(strstr(out_buf, "Hello, World!") != NULL);

    /* Termination: either CS:IP=0:0 (kernel ABORTed → IVT[20h]
     * → halt → emu detects 0:0 ?) or wedged on bootstub `halt`
     * HLT-JMP loop. We accept either as "clean". The emulator
     * stops returning success when it sees the literal CS:IP=0:0
     * pattern. */
    int wedged_on_halt = (emu_get_cs() == EMU_BOOT_SEG &&
                          mem[((uint32_t)EMU_BOOT_SEG << 4) +
                              emu_get_ip()] == 0xF4);
    fails += T_EXPECT(halted || wedged_on_halt);

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
