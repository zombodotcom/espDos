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
 * esp8086 — public API for the 8086 instruction interpreter (forked
 * from Adrian Cable's 8086tiny). Tests and firmware include the same
 * header so layout constants are shared.
 */

/* === Memory layout (1 MB + 64 KB margin; matches upstream 8086tiny) ===
 *
 *   0x00000 - 0x003FF   IVT (256 vectors × 4 bytes)
 *   0x00400 - 0x004FF   BIOS data area
 *   0x00500 - 0x005FF   bootstub (segment 0x0050)
 *   0x01000 - 0xEFFFF   DOS user space (kernel at seg 0x0100;
 *                       MEMSCAN walks up through here)
 *   0xF0000 - 0xF001F   register file (REGS_BASE; 32 bytes)
 *   0xF0100 - 0xF1EF1   8086tiny BIOS lookup tables (~7.6 KB)
 *   0xFFFFF             top of real-mode 1 MB
 *   0x100000-0x10FFEF   margin for natural seg+off overflow
 */
#define EMU_REGS_BASE      0xF0000u   /* mirrors REGS_BASE in esp8086.c */
#define EMU_BIOS_OFFSET    0x100u     /* BIOS lives at REGS_BASE+0x100 */
#define EMU_BIOS_SEG       (EMU_REGS_BASE >> 4)  /* = 0xF000 */

/* Where the kernel is loaded. The kernel binary was assembled with
 * `org 0x100` (the SCP-ASM `PUT 100H` directive in 86DOS.ASM line
 * 166): every absolute reference encodes its source-offset address,
 * so it must be loaded at offset 0x100 within its segment. */
#define EMU_KERNEL_SEG     0x0100u    /* = phys 0x1000 */
#define EMU_KERNEL_OFFSET  0x0100u    /* matches `org 0x100` in 86DOS.ASM */

/* Boot stub (asm/bootstub.asm) sits between the BIOS data area
 * (0x0400-0x04FF) and the kernel (0x1000+). Segment 0x0050 → phys
 * 0x0500. Assembled with `org 0` so we can load at any offset. */
#define EMU_BOOT_SEG       0x0050u    /* = phys 0x0500 */
#define EMU_BOOT_OFFSET    0x0000u


/* Wire up the regs8/regs16 pointers into the static mem[] array. The
 * memory itself is statically allocated by esp8086.c — no heap call.
 * Call once before issuing any emu_* operations. */
esp_err_t emu_alloc_mem(void);

/* Capacity of the emulated RAM (bytes). */
size_t emu_ram_size(void);

/* Direct access to the emulated RAM for copying kernel/BIOS bytes
 * into specific physical addresses. */
uint8_t *emu_mem(void);

/* Convenience: copy `n` bytes from `src` into emu memory at the
 * given seg:off (real-mode physical address = seg*16 + off). */
void emu_load(uint16_t seg, uint16_t off, const void *src, size_t n);

/* From esp8086.c (defined alongside the instruction loop). */
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
