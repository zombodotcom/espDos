#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * emu8086 — public API for the Adrian-Cable 8086tiny instruction
 * decoder + executor, adapted to be driven a step at a time from an
 * ESP-IDF main loop.
 *
 * Plan 2a: minimum surface to verify the component links and the
 * emulated RAM allocates correctly from PSRAM. The instruction-
 * stepping API (emu_init / emu_step) arrives in Plan 2b once we lift
 * the for-loop body out of 8086tiny's standalone main().
 */

/* Allocate the ~1MB emulated RAM from PSRAM and wire up the register
 * file pointers. Call once at startup before any other emu_* call. */
esp_err_t emu_alloc_mem(void);

/* Returns the size of 8086tiny's emulated RAM array (RAM_SIZE = 0x10FFF0). */
size_t emu_ram_size(void);

/* Direct access to 8086tiny's emulated RAM. NULL until emu_alloc_mem()
 * succeeds. Plan 3 BIOS handlers will use this to pull bytes for disk
 * reads, etc. */
const uint8_t *emu_ram(void);

/* Plan 2b will add:
 *   void emu_init(uint16_t cs, uint16_t ip);
 *   void emu_load(uint32_t phys_addr, const void *data, size_t n);
 *   int  emu_step(void);     // 0 = halted, 1 = running
 *   uint16_t emu_cs(void);
 *   uint16_t emu_ip(void);
 */
