/*
 * test_fininit_stack — single-step the kernel through FININIT and
 * capture the stack at the moment its `RETF` (CB) executes. The goal
 * is empirical: tell us *exactly* where the kernel intends to return
 * after init, so we know how to seize control.
 *
 * Approach:
 *   1. Boot kernel like test_kernel_banner does.
 *   2. Provide "1-1-81\r" as date input.
 *   3. After we see the date prompt printed, single-step. At each
 *      step, if CS:IP is near FININIT (file offset 0x166C → IP 0x176C
 *      since kernel is loaded at offset 0x100 within seg 0x100), log
 *      a trace line.
 *   4. When the byte at CS:IP is 0xCB (RETF), capture SS:SP and the
 *      next 4 bytes there (= IP, CS to be popped). Then step once
 *      more and report the new CS:IP.
 *
 * FININIT location (from build/kernel.lst):
 *      0x166C   E87EFD     CALL SETMEM
 *      0x166F   CB         RETF        <-- the byte we hunt for
 *
 * Loaded at seg 0x100, offset 0x100, so:
 *   FININIT_RETF_IP = 0x100 + 0x166F = 0x176F
 *
 * We assert nothing about the popped values — this test is purely
 * an investigation that prints findings.
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

/* Listing addr 0x1628 + load offset 0x100 = runtime IP 0x1728. */
#define FININIT_CALL_IP   0x1728
#define FININIT_RETF_IP   0x172B

static char     out_buf[8192];
static size_t   out_len;
static unsigned char *disk_img;
static size_t   disk_size;

/* Date input the kernel will consume: "1-1-81\r" */
/* Standard mm-dd-yy format. Now that the SCP `JP` mistranslation is
 * fixed, MYD parses digits properly and stops on the dash. */
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

    /* Phase 1: bulk-run with small chunks until date input has been
     * consumed. Then switch to single-step before FININIT. */
    int total = 0;
    const int budget = 4000000;
    while (total < budget) {
        if (date_pos >= 9) break;
        if (!emu_run_n(50)) { total += 50; break; }
        total += 50;
    }
    printf("phase1: ran %d insns, captured %zu bytes, date_pos=%zu\n",
           total, out_len, date_pos);
    /* Phase 1b: a bounded number of single steps, just enough for the
     * kernel to validate the date and run through SAVYR up to FININIT.
     * Done as single-step so we never overshoot the RETF. */

    /* Phase 2: single-step looking for FININIT. Keep a budget. */
    int saw_fininit_call = 0;
    int saw_retf         = 0;
    int steps = 0;
    int log_lines = 0;
    const int max_steps = 2000000;

    uint16_t pre_retf_ip = 0, pre_retf_cs = 0;
    uint16_t pre_retf_sp = 0, pre_retf_ss = 0;
    uint16_t popped_ip = 0, popped_cs = 0;

    while (steps < max_steps) {
        uint16_t cs = emu_get_cs();
        uint16_t ip = emu_get_ip();
        uint32_t pc = ((uint32_t)cs << 4) + ip;
        uint8_t  op = mem[pc];

        /* Are we in FININIT region (kernel CS, IP between FININIT_CALL
         * and FININIT_RETF+1)? */
        int in_fininit = (cs == EMU_KERNEL_SEG &&
                          ip >= FININIT_CALL_IP && ip <= FININIT_RETF_IP);

        if (in_fininit && log_lines < 40) {
            uint16_t sp = regs16[4];
            uint16_t ss = regs16[10];
            uint32_t stk = ((uint32_t)ss << 4) + sp;
            printf("  step CS:IP=%04x:%04x op=%02x %02x %02x %02x  "
                   "SS:SP=%04x:%04x  stk[0..15]=",
                   cs, ip, op, mem[pc+1], mem[pc+2], mem[pc+3],
                   ss, sp);
            for (int i = 0; i < 16; i++)
                printf("%02x ", mem[stk + i]);
            printf("\n");
            log_lines++;
            saw_fininit_call = 1;
        }

        /* Capture state RIGHT BEFORE the RETF executes. */
        if (in_fininit && ip == FININIT_RETF_IP && op == 0xCB) {
            uint16_t sp = regs16[4];
            uint16_t ss = regs16[10];
            uint32_t stk = ((uint32_t)ss << 4) + sp;
            pre_retf_ip = ip; pre_retf_cs = cs;
            pre_retf_sp = sp; pre_retf_ss = ss;
            popped_ip = (uint16_t)mem[stk] | ((uint16_t)mem[stk+1] << 8);
            popped_cs = (uint16_t)mem[stk+2] | ((uint16_t)mem[stk+3] << 8);
            printf("FOUND RETF at CS:IP=%04x:%04x  SS:SP=%04x:%04x\n",
                   cs, ip, ss, sp);
            printf("  stack[0..15]:");
            for (int i = 0; i < 16; i++) printf(" %02x", mem[stk + i]);
            printf("\n");
            printf("  predicted pop: IP=%04x CS=%04x  -> next %04x:%04x\n",
                   popped_ip, popped_cs, popped_cs, popped_ip);

            /* Step once to actually execute the RETF. */
            if (!emu_run_n(1)) {
                printf("emulator halted on RETF step!\n");
                saw_retf = 1;
                break;
            }
            steps++;
            uint16_t ncs = emu_get_cs();
            uint16_t nip = emu_get_ip();
            uint32_t npc = ((uint32_t)ncs << 4) + nip;
            printf("AFTER RETF: CS:IP=%04x:%04x  bytes there:", ncs, nip);
            for (int i = 0; i < 8; i++) printf(" %02x", mem[npc + i]);
            printf("\n");
            saw_retf = 1;
            break;
        }

        if (!emu_run_n(1)) {
            printf("emulator halted at CS:IP=%04x:%04x after %d single-steps\n",
                   cs, ip, steps);
            break;
        }
        steps++;
    }

    printf("---- summary ----\n");
    printf("date_pos=%zu  saw_fininit_call=%d  saw_retf=%d  steps=%d\n",
           date_pos, saw_fininit_call, saw_retf, steps);
    if (saw_retf) {
        printf("FININIT RETF at CS:IP=%04x:%04x  popped IP=%04x CS=%04x  "
               "next instruction landed at %04x:%04x\n",
               pre_retf_cs, pre_retf_ip, popped_ip, popped_cs,
               emu_get_cs(), emu_get_ip());
    }
    /* Print full captured output for context. */
    printf("---- captured output (%zu bytes) ----\n", out_len);
    /* Replace non-printables for visibility. */
    for (size_t i = 0; i < out_len && i < 600; i++) {
        unsigned char c = (unsigned char)out_buf[i];
        if (c == '\r') printf("\\r");
        else if (c == '\n') printf("\\n\n");
        else if (c < 32 || c >= 127) printf("\\x%02x", c);
        else putchar(c);
    }
    printf("\n----\n");

    /* DATBUF: listing addr 0x18B3 (post JP-fix). Loaded at +0x100. */
    uint32_t db = ((uint32_t)EMU_KERNEL_SEG << 4) + 0x18B3 + 0x100;
    printf("DATBUF (kernel:18FB) =");
    for (int i = 0; i < 14; i++) printf(" %02x", mem[db + i]);
    printf("\n");

    free(disk_img);
    /* This is an investigation — success means we reached the RETF
     * and observed where it returns. The bootstub's pre-pushed far
     * pointer (cs, LOADER_OFFSET) is what we expect on the stack. */
    int ok = saw_retf
          && popped_cs == EMU_BOOT_SEG
          && popped_ip == 0x0100;
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
