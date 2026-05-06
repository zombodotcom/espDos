#pragma once

#include <stddef.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#endif

/*
 * emu8086 — public API for the Adrian-Cable 8086tiny instruction
 * decoder + executor, adapted to be driven a step at a time from an
 * ESP-IDF main loop. Tests include this same header on host.
 */

/* === Memory layout constants (single source of truth) ===
 *
 * These define WHERE inside the 8086 emulator's flat memory we put
 * each thing. Both firmware and host tests use the same values so
 * a passing test corresponds to firmware behavior.
 *
 * Total emulated RAM is 192 KB (set in 8086tiny.c via RAM_SIZE).
 * Layout:
 *
 *   0x00000 - 0x00FFF   IVT + BIOS data area (low memory; segment 0)
 *   0x01000 - 0x1FFFF   DOS working memory: kernel at seg 0x0100,
 *                       user .COM segments above that (124 KB)
 *   0x20000 - 0x2001F   register file (32 bytes; 8086tiny puts CPU
 *                       state at REGS_BASE)
 *   0x20100 - 0x21FFF   8086tiny BIOS image (~8 KB; the instruction
 *                       decoder's lookup tables live inside it)
 *   0x22000 - 0x2FFFF   slack
 */
#define EMU_REGS_BASE      0x20000u   /* mirrors REGS_BASE in 8086tiny.c */
#define EMU_BIOS_OFFSET    0x100u     /* BIOS lives at regs8 + 0x100 */
#define EMU_BIOS_SEG       (EMU_REGS_BASE >> 4)  /* = 0x2000 */

/* Where we load the kernel inside emu memory. The kernel binary was
 * assembled with `org 0x100` (the SCP-ASM `PUT 100H` directive in
 * 86DOS.ASM line 166), which tells the assembler all internal label
 * values assume the binary is loaded at offset 0x100 within its
 * segment. Relative jumps don't care, but absolute references like
 * `MOV [DATE], AX` encode the source-offset address — if we load at
 * offset 0 instead of 0x100, every absolute reference is off by 256
 * bytes and the kernel reads/writes garbage. So load at 0x100. */
#define EMU_KERNEL_SEG     0x0100u    /* = phys 0x1000 */
#define EMU_KERNEL_OFFSET  0x0100u    /* matches `org 0x100` in 86DOS.ASM */


/* Allocate the emulated 8086 RAM (~320 KB) from PSRAM if available
 * else internal DRAM, and wire up regs8/regs16 pointers. Call once. */
esp_err_t emu_alloc_mem(void);

/* Capacity (bytes) of the emulated RAM allocation. */
size_t emu_ram_size(void);

/* Direct access to the emulated RAM for copying kernel/BIOS bytes
 * into specific physical addresses. NULL until emu_alloc_mem(). */
uint8_t *emu_mem(void);

/* Convenience: copy `n` bytes from `src` into emu memory at the
 * given seg:off (8086 real-mode physical address = seg*16 + off). */
void emu_load(uint16_t seg, uint16_t off, const void *src, size_t n);

/* From 8086tiny.c (defined alongside the instruction loop). */
void           emu_init_state(void);
void           emu_load_bios_tables(void);
void           emu_set_cs_ip(unsigned short cs, unsigned short ip);
unsigned short emu_get_cs(void);
unsigned short emu_get_ip(void);
unsigned short emu_get_ax(void);

/* Run at most `max_steps` instructions. Returns:
 *   0 — emulator halted (CS:IP became 0:0)
 *   1 — still running (max_steps reached, can be called again)  */
int emu_run_n(int max_steps);
