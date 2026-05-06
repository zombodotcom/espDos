#include <stdio.h>
#include <string.h>

#include "test_helpers.h"

/* These constants must match firmware/components/emu8086/8086tiny.c. */
#define EMU_REGS_BASE  0x20000u

/* Wire 8086tiny's regs8/regs16 pointers into the static mem[] array.
 * (The firmware does this in emu_alloc_mem(); on host we duplicate
 * here.) */
extern unsigned char *regs8;
extern unsigned short *regs16;

static void t_init_pointers(void) {
    regs8  = mem + EMU_REGS_BASE;
    regs16 = (unsigned short *)regs8;
}

int t_load_bios(void) {
    t_init_pointers();
    FILE *f = fopen("../../third_party/8086tiny/bios.bin", "rb");
    if (!f) {
        f = fopen("third_party/8086tiny/bios.bin", "rb");
    }
    if (!f) {
        perror("bios.bin");
        return 1;
    }
    /* BIOS lands at regs8+0x100 = mem[REGS_BASE+0x100]. */
    size_t n = fread(mem + EMU_REGS_BASE + 0x100, 1, 8192, f);
    fclose(f);
    if (n < 0x1000) {
        fprintf(stderr, "BIOS too short: %zu bytes\n", n);
        return 1;
    }
    emu_load_bios_tables();
    return 0;
}

void t_load(uint16_t seg, uint16_t off, const void *bytes, size_t n) {
    uint32_t phys = ((uint32_t)seg << 4) + off;
    memcpy(mem + phys, bytes, n);
}

void t_dump_state(const char *label) {
    uint16_t cs = emu_get_cs(), ip = emu_get_ip();
    uint32_t phys = ((uint32_t)cs << 4) + ip;
    printf("  [%s] CS:IP=%04x:%04x  AX=%04x  bytes=%02x %02x %02x\n",
           label, cs, ip, emu_get_ax(),
           mem[phys], mem[phys+1], mem[phys+2]);
}

int t_check(const char *expr, int cond, const char *file, int line) {
    if (cond) {
        printf("  PASS: %s\n", expr);
        return 0;
    } else {
        printf("  FAIL: %s  (%s:%d)\n", expr, file, line);
        return 1;
    }
}
