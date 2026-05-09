/*
 * test_shell_mandel — verifies SHELL's external_load actually launches
 * a transient. Boots bootstub_shell.bin (which loads SHELL.COM into
 * USER_SEG), drives the date prompt + the typed "MANDEL\r" command,
 * and asserts MANDEL output appears.
 *
 * Catches the kind of regression where a SHELL-internal path that
 * passes manual smoke testing on hardware quietly breaks for every
 * launch (e.g., reading shell_sp_save through the wrong segment, or
 * hitting kernel SEQRD's SECSIZ-divide trap).
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

static char     out_buf[262144];
static size_t   out_len;
static unsigned char *disk_img;
static size_t   disk_size;

/* Date "1-1-80\r" then "MANDEL\r" then nothing (BIOSIN returns 0x0D
 * / STAT returns 0 forever after — MANDEL doesn't read input). */
static const char *script   = "1-1-80\rMANDEL\r";
static size_t      script_pos = 0;

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
        if (script[script_pos]) {
            regs8[R_AL] = (uint8_t)script[script_pos++];
        } else {
            regs8[R_AL] = 0x0D;
        }
        break;
    case BIOS_OFF_STAT:
        regs8[R_AL] = script[script_pos] ? 0xFF : 0x00;
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
    if (load_blob("../../build/bootstub_shell.bin",
                  EMU_BOOT_SEG, EMU_BOOT_OFFSET)            != 0) return 2;
    if (load_blob("../../build/kernel.bin",
                  EMU_KERNEL_SEG, EMU_KERNEL_OFFSET)        != 0) return 2;

    out_len = 0; out_buf[0] = 0;
    script_pos = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    int total = 0;
    const int budget = 30000000;   /* generous: kernel boot + MANDEL render */
    int halted = 0;

    /* Run in chunks so we can spot CS:IP=0:0 halts and short-circuit. */
    while (total < budget) {
        if (!emu_run_n(5000)) { halted = 1; break; }
        total += 5000;
    }

    printf("ran %d instructions, captured %zu bytes\n", total, out_len);
    printf("end CS:IP = %04x:%04x  halted=%d  script_pos=%zu\n",
           emu_get_cs(), emu_get_ip(), halted, script_pos);

    /* Print a tail of the output so a failure is debuggable. */
    size_t tail_start = out_len > 600 ? out_len - 600 : 0;
    printf("---- last %zu bytes of output ----\n", out_len - tail_start);
    fwrite(out_buf + tail_start, 1, out_len - tail_start, stdout);
    printf("\n---- end ----\n");

    /* Sanity: kernel banner + SHELL prompt + MANDEL output. */
    fails += T_EXPECT(strstr(out_buf, "86-DOS") != NULL);
    fails += T_EXPECT(strstr(out_buf, "A>")     != NULL);
    /* MANDEL prints a 78x24 ASCII fractal using density chars from
     * the ramp ' .:-=+*#%@'. The signature: lots of '@' (cardioid
     * core) + a few '*' / '#'. We just check for the @ pattern. */
    fails += T_EXPECT(strstr(out_buf, "@@@") != NULL);

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
