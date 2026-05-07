/*
 * test_kernel_banner — runs Tim Paterson's 86-DOS kernel for enough
 * instructions to print its banner. Mirrors the firmware path:
 *   - load bootstub.bin at BOOT_SEG:BOOT_OFFSET
 *   - load kernel.bin at KERNEL_SEG:KERNEL_OFFSET
 *   - drive the emulator with a BIOS callback that captures OUT bytes
 *   - assert "86-DOS version 1.00" appears in captured output
 *
 * With the Tier 1 memory rework (full 1 MB + 64 KB, REGS_BASE at
 * 0xF0000), MEMSCAN terminates naturally — no runtime patch needed.
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
        /* Banner happens before the date prompt. We're testing for
         * banner only, so any input here would be after we've
         * already passed/failed. Return CR to satisfy the read. */
        regs8[R_AL] = 0x0D;
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

static int load_blob(const char *path, uint16_t seg, uint16_t off,
                     size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    t_load(seg, off, buf, n);
    if (out_n) *out_n = n;
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

    size_t bs_n, k_n;
    if (load_blob("../../build/bootstub.bin",
                  EMU_BOOT_SEG, EMU_BOOT_OFFSET, &bs_n)     != 0) return 2;
    if (load_blob("../../build/kernel.bin",
                  EMU_KERNEL_SEG, EMU_KERNEL_OFFSET, &k_n)  != 0) return 2;

    out_len = 0; out_buf[0] = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    int total = 0;
    const int budget = 1500000;
    const int chunk  = 5000;
    while (total < budget) {
        if (!emu_run_n(chunk)) { total += chunk; break; }
        total += chunk;
        if (strstr(out_buf, "(m-d-y)")) break;
    }
    printf("  ran %d instructions, captured %zu bytes:\n", total, out_len);
    fwrite(out_buf, 1, out_len, stdout);
    if (out_len && out_buf[out_len - 1] != '\n') printf("\n");
    printf("  ----\n");

    fails += T_EXPECT(strstr(out_buf, "86-DOS") != NULL);
    fails += T_EXPECT(strstr(out_buf, "version 1.00") != NULL);
    fails += T_EXPECT(strstr(out_buf, "Seattle Computer Products") != NULL);

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
