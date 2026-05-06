/*
 * test_kernel_banner — runs Tim Paterson's 86-DOS kernel for enough
 * instructions to print its banner. Mirrors the firmware path:
 *   - load bootstub.bin at BOOT_SEG:BOOT_OFFSET
 *   - load kernel.bin at KERNEL_SEG:KERNEL_OFFSET
 *   - apply the MEMSCAN short-circuit patch
 *   - drive the emulator with a BIOS callback that captures OUT bytes
 *   - assert "86-DOS version 1.00" appears in captured output
 *
 * The MEMSCAN patch matches the firmware's logic verbatim — if this
 * passes on host, hardware will too.
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

/* Trace each BIOSOUT call so we can see what the kernel's OUTMES
 * is reading. Set DEBUG_OUT=1 to enable. */
#define DEBUG_OUT 1
#define R_SI 6
static unsigned out_idx;

static void kernel_bios(unsigned short ip) {
    uint8_t  al = regs8[R_AL];
    uint16_t bx = regs16[R_BX];
    uint16_t cx = regs16[1];
    uint16_t dx = regs16[2];
    uint16_t ds = regs16[R_DS];

    switch (ip) {
    case BIOS_OFF_OUT:
        if (DEBUG_OUT) {
            uint16_t si = regs16[R_SI];
            uint16_t cs = regs16[9];
            uint16_t ds = regs16[R_DS];
            /* Log only the boundary: last few good chars + first
             * garbage chars. Banner = ~75 chars, then DATMES first
             * 16 chars, then garbage. Print outs around #88..#100. */
            if (out_idx == 90) {
                /* First garbage char. Dump mem around DATMES position
                 * 16 to see what overwrote it. DATMES at file offset
                 * 0x16b6 → mem[0x100*16 + 0x100 + 0x16b6] = 0x27B6.
                 * Position 16 is at 0x27C6. */
                fprintf(stderr, "  ---- mem dump around DATMES+16 ----\n");
                /* Dump 64 bytes starting at DATMES (=0x27B6).
                 * Compare to the on-disk kernel bytes in the file.
                 * Where they diverge tells us where the kernel
                 * scribbled. */
                FILE *kf = fopen("../../build/kernel.bin", "rb");
                fseek(kf, 0x16B6, SEEK_SET);
                uint8_t kbytes[64];
                fread(kbytes, 1, sizeof kbytes, kf);
                fclose(kf);
                for (int row = 0; row < 4; row++) {
                    fprintf(stderr, "  +%02x: actual=", row * 16);
                    for (int i = 0; i < 16; i++)
                        fprintf(stderr, "%02x ", mem[0x27B6 + row*16 + i]);
                    fprintf(stderr, "\n      expect=");
                    for (int i = 0; i < 16; i++)
                        fprintf(stderr, "%02x ", kbytes[row*16 + i]);
                    fprintf(stderr, "\n      diff  =");
                    for (int i = 0; i < 16; i++) {
                        uint8_t a = mem[0x27B6 + row*16 + i];
                        uint8_t e = kbytes[row*16 + i];
                        fprintf(stderr, "%s ", a == e ? ".." : "XX");
                    }
                    fprintf(stderr, "\n");
                }
            }
            if (out_idx >= 85 && out_idx <= 95) {
                fprintf(stderr, "  [out#%03u] AL=%02x ('%c')  "
                                "DS:SI=%04x:%04x\n",
                        out_idx, al, (al >= 32 && al < 127) ? al : '.',
                        ds, si);
            }
            out_idx++;
        }
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

/* Locate MEMSCAN by signature in the loaded kernel and overwrite its
 * first 5 bytes with `MOV CX, 0x3000; JMP +0x0E`. Returns offset
 * within kernel (or 0 if not found). */
static size_t patch_memscan(uint16_t seg, uint16_t off, size_t kernel_size) {
    /* New signature after the preprocessor's R13b expansion of JZ
     * targets to Jnotcc+JMP pairs. Must match the firmware's
     * espdos.c version exactly. */
    static const uint8_t sig[] = {
        0x41, 0x75, 0x02, 0xEB, 0x12, 0x8E, 0xD9, 0x8A, 0x07,
        0xF6, 0xD0, 0x88, 0x07, 0x3A, 0x07, 0xF6, 0xD0, 0x88,
        0x07, 0x75, 0x02, 0xEB, 0xE9
    };
    uint32_t base = ((uint32_t)seg << 4) + off;
    for (size_t i = 0; i + sizeof sig <= kernel_size; i++) {
        if (memcmp(&mem[base + i], sig, sizeof sig) == 0) {
            uint8_t patch[5] = { 0xB9, 0x00, 0x30, 0xEB, 0x12 };
            memcpy(&mem[base + i], patch, sizeof patch);
            return i;
        }
    }
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

    size_t patch_off =
        patch_memscan(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET, k_n);
    if (!patch_off) {
        printf("  FAIL: MEMSCAN signature not found in kernel\n");
        return 1;
    }
    printf("  patched MEMSCAN at kernel offset 0x%04zx\n", patch_off);

    out_len = 0; out_buf[0] = 0;
    t_set_bios_callback(kernel_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    /* Single-step the kernel and detect the very first instruction
     * that writes to mem[0x27C6] — DATMES position 16, the byte
     * that the kernel later reads back as 0xb0 instead of 't'.
     * Log the offending CS:IP, the surrounding instruction bytes,
     * and the value being written. */
    extern unsigned short reg_ip;
    int total = 0;
    uint8_t before = mem[0x27C6];
    int caught = 0;
    while (total < 500000) {
        uint16_t cs_pre = emu_get_cs(), ip_pre = emu_get_ip();
        if (!emu_run_n(1)) break;
        total++;
        uint8_t now = mem[0x27C6];
        if (!caught && now != before) {
            caught = 1;
            uint32_t phys = ((uint32_t)cs_pre << 4) + ip_pre;
            printf("\n*** mem[0x27C6] changed from 0x%02x to 0x%02x at "
                   "step %d ***\n", before, now, total);
            printf("    instruction CS:IP=%04x:%04x  bytes=%02x %02x %02x %02x %02x\n",
                   cs_pre, ip_pre,
                   mem[phys], mem[phys+1], mem[phys+2],
                   mem[phys+3], mem[phys+4]);
            printf("    regs: AX=%04x BX=%04x CX=%04x DX=%04x  "
                   "SI=%04x DI=%04x BP=%04x SP=%04x  "
                   "DS=%04x ES=%04x SS=%04x\n",
                   regs16[0], regs16[3], regs16[1], regs16[2],
                   regs16[6], regs16[7], regs16[5], regs16[4],
                   regs16[11], regs16[8], regs16[10]);
            before = now;
        }
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
