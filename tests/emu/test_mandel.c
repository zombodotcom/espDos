/*
 * test_mandel — exercises the Mandelbrot transient.
 *
 * Boots bootstub_mandel.bin (= same bootstub but with the
 * loader_mandel.bin variant embedded, which reads sector 12 = MANDEL.COM
 * off the FAT12 disk image instead of cluster 2's HELLO.COM). Drives
 * the date prompt through BIOSIN, captures BIOSOUT, then asserts that
 * after the date prompt the captured stream contains a recognizable
 * 78-wide x 24-tall ASCII Mandelbrot grid (CR/LF terminated rows,
 * majority of cells non-space).
 *
 * We don't pin exact pixels — the Q4.12 fixed-point may drift one or
 * two cells around the cardioid boundary depending on rounding — but
 * we do pin structure: HEIGHT rows, each WIDTH chars wide, and a
 * substantial fraction of non-space pixels (the Mandelbrot bulb
 * occupies a sizable fraction of the [-2.0, 0.5] x [-1.0, 1.0] window).
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

#define MAND_WIDTH   78
#define MAND_HEIGHT  24

static char     out_buf[65536];
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

/* Find the first MAND_HEIGHT consecutive lines (ending in LF) that
 * look like Mandelbrot rows: each at least MAND_WIDTH chars long
 * (excluding CR/LF) and consisting of printable ASCII. Returns a
 * pointer to the start of the grid, or NULL. */
static const char *find_grid(const char *buf, size_t n) {
    /* Scan the buffer line by line. */
    const char *p = buf;
    const char *end = buf + n;
    while (p < end) {
        /* Try to consume MAND_HEIGHT rows starting at p. */
        const char *q = p;
        int ok = 1;
        for (int row = 0; row < MAND_HEIGHT; row++) {
            const char *line_start = q;
            int line_len = 0;
            while (q < end && *q != '\n' && *q != '\r') { q++; line_len++; }
            if (line_len < MAND_WIDTH) { ok = 0; break; }
            (void)line_start;
            /* Skip CR and/or LF. */
            if (q < end && *q == '\r') q++;
            if (q < end && *q == '\n') q++;
        }
        if (ok) return p;
        /* Otherwise advance to next line and retry. */
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return NULL;
}

int main(void) {
    int fails = 0;

    if (t_load_bios()                                       != 0) return 2;
    if (load_disk("../../build/disk.img")                   != 0) return 2;
    if (load_blob("../../build/bootstub_mandel.bin",
                  EMU_BOOT_SEG, EMU_BOOT_OFFSET)            != 0) return 2;
    if (load_blob("../../build/kernel.bin",
                  EMU_KERNEL_SEG, EMU_KERNEL_OFFSET)        != 0) return 2;

    out_len = 0; out_buf[0] = 0;
    date_pos = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    /* Mandelbrot is heavy: 78*24 pixels * up to 24 iterations per
     * pixel * dozens of host instructions per iteration. Budget is
     * generous. Stop early once we've captured a full grid. */
    int total = 0;
    const int budget = 30000000;
    int halted = 0;

    /* Phase 1: run until date input fully consumed. */
    while (total < budget) {
        if (date_pos >= 9) break;
        if (!emu_run_n(50)) { total += 50; halted = 1; break; }
        total += 50;
    }
    printf("phase1 (until date consumed): total=%d, date_pos=%zu\n",
           total, date_pos);

    /* Phase 2: run until we've collected a recognizable grid OR halt. */
    int saw_grid_at = -1;
    while (!halted && total < budget) {
        if (!emu_run_n(2000)) { total += 2000; halted = 1; break; }
        total += 2000;
        if (saw_grid_at < 0 && find_grid(out_buf, out_len)) {
            saw_grid_at = total;
            /* Once we see a grid, give it 200k more to terminate. */
        }
        if (saw_grid_at >= 0 && total - saw_grid_at > 400000) break;
    }

    printf("ran %d instructions, captured %zu bytes; grid first at +%d\n",
           total, out_len, saw_grid_at);
    printf("end CS:IP = %04x:%04x  halted=%d\n",
           emu_get_cs(), emu_get_ip(), halted);

    /* Echo the captured output verbatim — terminal-friendly so a
     * human can sanity-check the Mandelbrot by eye. Non-printables
     * are escaped (CR shown as \r, LF as a newline). */
    printf("---- BIOSOUT capture begin ----\n");
    for (size_t i = 0; i < out_len; i++) {
        unsigned char c = (unsigned char)out_buf[i];
        if (c == '\r')      { /* swallow */ }
        else if (c == '\n') putchar('\n');
        else if (c < 32 || c >= 127) printf("\\x%02x", c);
        else                putchar(c);
    }
    printf("\n---- BIOSOUT capture end ----\n");

    /* Banner + date prompt sanity. */
    fails += T_EXPECT(strstr(out_buf, "86-DOS") != NULL);
    fails += T_EXPECT(strstr(out_buf, "(m-d-y)") != NULL ||
                      strstr(out_buf, "Date")    != NULL ||
                      strstr(out_buf, "DATE")    != NULL);

    /* Locate the Mandelbrot grid. */
    const char *grid = find_grid(out_buf, out_len);
    fails += T_EXPECT(grid != NULL);

    if (grid) {
        /* Count non-space cells across the WIDTH*HEIGHT grid. */
        int total_cells = 0;
        int nonspace = 0;
        const char *q = grid;
        const char *end = out_buf + out_len;
        for (int row = 0; row < MAND_HEIGHT && q < end; row++) {
            int col = 0;
            while (q < end && *q != '\r' && *q != '\n' && col < MAND_WIDTH) {
                total_cells++;
                if (*q != ' ') nonspace++;
                q++; col++;
            }
            /* Skip remainder of line + CR/LF. */
            while (q < end && *q != '\n') q++;
            if (q < end) q++;
        }
        printf("grid stats: %d cells counted, %d non-space (%.1f%%)\n",
               total_cells, nonspace,
               total_cells ? 100.0 * nonspace / total_cells : 0.0);
        /* Mandelbrot's main bulb fills a substantial chunk of the
         * frame; we accept anything >= 30% non-space as proof the
         * iteration produced visible structure (not all-spaces or
         * all-'@'). */
        fails += T_EXPECT(total_cells >= MAND_WIDTH * MAND_HEIGHT - 4);
        fails += T_EXPECT(nonspace * 100 >= total_cells * 30);
        /* And not all-fill either (would mean iteration isn't actually
         * escaping anywhere). */
        fails += T_EXPECT(nonspace * 100 <= total_cells * 95);
    }

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
