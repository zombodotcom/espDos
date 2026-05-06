#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * emu8086 — public API for the Adrian-Cable 8086tiny instruction
 * decoder + executor, adapted to be driven a step at a time from an
 * ESP-IDF main loop.
 */

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
