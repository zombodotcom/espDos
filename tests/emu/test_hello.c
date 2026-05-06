/*
 * test_hello — end-to-end pipeline confidence harness.
 *
 * Runs build/hello.bin through:
 *   bootstub.bin (sets up IVT + DS:SI + JMPs to KERNEL_SEG:KERNEL_OFFSET)
 *     → hello.bin (CALL FAR BIOSSEG:N for OUT/IN/READ)
 *       → BIOSSEG trap in 8086tiny.c
 *         → our bios_handle_call (below) which captures output,
 *           feeds scripted input, and reads sectors from build/disk.img
 *
 * Expected output (byte-exact):
 *   "espdos hello\r\n"
 *   "press any key: >X\r\n"           (where X is the scripted input char)
 *   "disk[0]=FE\r\n"                   (FE = media byte from build_disk.py)
 *
 * If this passes, the boot-stub-then-payload pipeline works end-to-end.
 * Any subsequent kernel failure can be attributed to the kernel's own
 * init assumptions (PSP, ENDMEM, COMMAND.COM, ...) rather than plumbing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_helpers.h"

/* BIOSSEG offset constants (mirror firmware/components/bios/include/bios.h). */
#define BIOS_OFF_STAT     0x03
#define BIOS_OFF_IN       0x06
#define BIOS_OFF_OUT      0x09
#define BIOS_OFF_READ     0x15
#define BIOS_OFF_WRITE    0x18

/* 8086tiny register-file indices. */
#define R_AL  0
#define R_AH  1
#define R_AX  0
#define R_CX  1
#define R_DX  2
#define R_BX  3
#define R_DS  11
#define F_CF  40

extern unsigned char  *regs8;
extern unsigned short *regs16;

/* ----- Captured I/O state ----- */

static char     out_buf[1024];
static size_t   out_len;
static const char *in_script;
static size_t   in_pos;
static unsigned char *disk_img;
static size_t   disk_size;

static void out_put(uint8_t c) {
    if (out_len < sizeof(out_buf) - 1) {
        out_buf[out_len++] = (char)c;
        out_buf[out_len]   = 0;
    }
}

static uint8_t in_next(void) {
    if (!in_script || !in_script[in_pos]) return 0;
    return (uint8_t)in_script[in_pos++];
}

static int load_disk(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    disk_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    disk_img = malloc(disk_size);
    if (!disk_img) { fclose(f); return -1; }
    fread(disk_img, 1, disk_size, f);
    fclose(f);
    return 0;
}

/* ----- BIOS dispatch ----- */

static void hello_bios(unsigned short ip) {
    uint8_t  al  = regs8[R_AL];
    uint16_t bx  = regs16[R_BX];
    uint16_t cx  = regs16[R_CX];
    uint16_t dx  = regs16[R_DX];
    uint16_t ds  = regs16[R_DS];

    switch (ip) {
    case BIOS_OFF_OUT:
        out_put(al);
        break;
    case BIOS_OFF_IN:
        regs8[R_AL] = in_next();
        break;
    case BIOS_OFF_STAT:
        regs8[R_AL] = (in_script && in_script[in_pos]) ? 0xFF : 0x00;
        break;
    case BIOS_OFF_READ: {
        if (al != 0 || !disk_img) {
            regs8[F_CF] = 1;
            break;
        }
        uint32_t off = (uint32_t)dx * 512;
        uint32_t len = (uint32_t)cx * 512;
        if (off + len > disk_size) {
            regs8[F_CF] = 1;
            break;
        }
        uint32_t buf_phys = ((uint32_t)ds << 4) + bx;
        memcpy(&mem[buf_phys], &disk_img[off], len);
        regs8[F_CF] = 0;
        break;
    }
    case BIOS_OFF_WRITE:
        /* hello.asm doesn't write — fail loud if it tries. */
        printf("  unexpected BIOS_WRITE ip=%04x\n", ip);
        regs8[F_CF] = 1;
        break;
    default:
        printf("  unexpected BIOSSEG entry ip=%04x\n", ip);
        break;
    }
}

/* ----- Test ----- */

static int load_payload(const char *path, uint16_t seg, uint16_t off) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char buf[4096];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    t_load(seg, off, buf, n);
    printf("  loaded %s (%zu bytes) at %04x:%04x\n", path, n, seg, off);
    return 0;
}

int main(void) {
    int fails = 0;

    if (t_load_bios() != 0)                           return 2;
    if (load_disk("../../build/disk.img") != 0)       return 2;
    if (load_payload("../../build/bootstub.bin",
                     EMU_BOOT_SEG, EMU_BOOT_OFFSET))  return 2;
    if (load_payload("../../build/hello.bin",
                     EMU_KERNEL_SEG, EMU_KERNEL_OFFSET)) return 2;

    in_script = "X";   /* one keypress */
    out_len = 0;
    out_buf[0] = 0;
    t_set_bios_callback(hello_bios);

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    /* Run until hello.bin reaches its `done: jmp done` tight loop —
     * detected as IP unchanged across two consecutive single-step
     * runs while CS == KERNEL_SEG. Cap at 10000 instructions so a
     * runaway test doesn't hang. */
    uint16_t prev_cs = 0, prev_ip = 0;
    int wedged = 0;
    int total = 0;
    for (total = 0; total < 10000; total++) {
        if (!emu_run_n(1)) break;
        uint16_t cs = emu_get_cs(), ip = emu_get_ip();
        if (cs == EMU_KERNEL_SEG && cs == prev_cs && ip == prev_ip) {
            if (++wedged >= 3) break;
        } else {
            wedged = 0;
        }
        prev_cs = cs;
        prev_ip = ip;
    }

    printf("  ran %d instructions, ended at CS:IP=%04x:%04x\n",
           total, prev_cs, prev_ip);
    printf("  captured %zu bytes of output:\n", out_len);
    /* Print with control-char escapes so the layout is readable. */
    printf("  ┌────────────\n");
    for (size_t i = 0; i < out_len; i++) {
        char c = out_buf[i];
        if (c == '\r')      printf("  │ \\r\n");
        else if (c == '\n') printf("  │ \\n\n");
        else                printf("  │ %c\n", c);
    }
    printf("  └────────────\n");

    /* Byte-exact verification. The disk image's media byte is 0xFE
     * (build_disk.py MEDIA_BYTE) at sector-0-offset-0, so disk[0] = FE.
     * The echo char comes from the script ('X' above). */
    /* Sector 1 is FAT 1; byte 0 is the media descriptor (0xFE per
     * tools/build_disk.py). Reading it back proves the BIOSREAD
     * path actually pulls bytes from the disk image, not from
     * uninitialized buffer memory. */
    const char *expected =
        "espdos hello\r\n"
        "press any key: >X\r\n"
        "fat[0]=FE\r\n";

    if (strcmp(out_buf, expected) == 0) {
        printf("  PASS: output matches expected\n");
    } else {
        printf("  FAIL: output mismatch\n");
        printf("  expected: \"%s\"\n", expected);
        printf("  actual  : \"%s\"\n", out_buf);
        fails++;
    }

    free(disk_img);
    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
