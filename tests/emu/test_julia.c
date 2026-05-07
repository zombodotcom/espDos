/*
 * test_julia — exercises the Julia transient.
 *
 * Boots bootstub_julia.bin (loader_julia variant reads sector 15
 * for 3 sectors = JULIA.COM, 1082 bytes). Drives the date prompt
 * through BIOSIN, captures BIOSOUT, then asserts:
 *
 *   - banner + date prompt show up (kernel boots cleanly)
 *   - the captured stream contains ANSI escape sequences (ESC '[')
 *   - we see at least one ESC[H (cursor home) — frame separator
 *   - we see at least 2000 ANSI color set sequences (one per pixel
 *     for 1+ frames worth of output; full frame is 78*24=1872 pixels)
 *
 * We don't pin exact frames or pixel layout — Q4.12 fixed-point may
 * drift around the c-orbit boundary — but we pin the structure
 * that makes JULIA recognizable: ANSI color codes streaming, with
 * frame separators between renders.
 *
 * Budget: Julia is heavy. Each frame is ~500K instructions on its
 * own; we just need ONE complete frame's worth of output to assert.
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
static const char *date_input = "01-01-81\r";
static size_t      date_pos   = 0;

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

/* Count distinct ANSI escape sequences in `buf`. We treat any ESC '['
 * as the start of a sequence; counting open-brackets is a fine
 * proxy. */
static int count_ansi_seqs(const char *buf, size_t n) {
    int seqs = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if ((unsigned char)buf[i] == 0x1B && buf[i+1] == '[') seqs++;
    }
    return seqs;
}

/* Look for a frame separator: ESC '[' 'H'. */
static int has_cursor_home(const char *buf, size_t n) {
    for (size_t i = 0; i + 2 < n; i++) {
        if ((unsigned char)buf[i] == 0x1B &&
            buf[i+1] == '[' && buf[i+2] == 'H')
            return 1;
    }
    return 0;
}

int main(void) {
    int fails = 0;

    if (t_load_bios()                                       != 0) return 2;
    if (load_disk("../../build/disk.img")                   != 0) return 2;
    if (load_blob("../../build/bootstub_julia.bin",
                  EMU_BOOT_SEG, EMU_BOOT_OFFSET)            != 0) return 2;
    if (load_blob("../../build/kernel.bin",
                  EMU_KERNEL_SEG, EMU_KERNEL_OFFSET)        != 0) return 2;

    out_len = 0; out_buf[0] = 0;
    date_pos = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    int total = 0;
    const int budget = 60000000;   /* 60M instructions: kernel + a couple frames */
    int halted = 0;

    /* Phase 1: run until date input fully consumed. */
    while (total < budget) {
        if (date_pos >= 9) break;
        if (!emu_run_n(50)) { total += 50; halted = 1; break; }
        total += 50;
    }
    printf("phase1 (date consumed): total=%d, date_pos=%zu\n",
           total, date_pos);

    /* Phase 2: run until we've seen at least one full frame's worth of
     * ANSI sequences (>= 2000 ESC[ codes), plus a cursor-home. */
    int saw_at = -1;
    while (!halted && total < budget) {
        if (!emu_run_n(2000)) { total += 2000; halted = 1; break; }
        total += 2000;
        if (saw_at < 0) {
            int seqs = count_ansi_seqs(out_buf, out_len);
            if (seqs >= 2000 && has_cursor_home(out_buf, out_len)) {
                saw_at = total;
                /* Give it a bit more to round out the assertion window. */
            }
        }
        if (saw_at >= 0 && total - saw_at > 200000) break;
    }

    int seqs = count_ansi_seqs(out_buf, out_len);
    printf("ran %d instructions, captured %zu bytes, %d ANSI sequences "
           "(first frame complete at +%d)\n",
           total, out_len, seqs, saw_at);
    printf("end CS:IP = %04x:%04x  halted=%d\n",
           emu_get_cs(), emu_get_ip(), halted);

    /* Banner + date prompt sanity. */
    fails += T_EXPECT(strstr(out_buf, "86-DOS") != NULL);
    fails += T_EXPECT(strstr(out_buf, "(m-d-y)") != NULL ||
                      strstr(out_buf, "Date")    != NULL ||
                      strstr(out_buf, "DATE")    != NULL);

    /* JULIA-specific assertions. */
    fails += T_EXPECT(seqs >= 2000);
    fails += T_EXPECT(has_cursor_home(out_buf, out_len));

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
