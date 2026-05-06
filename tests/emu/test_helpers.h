#pragma once

#include <stddef.h>
#include <stdint.h>

/* Pull in the layout constants and emu_* declarations from the
 * firmware header so tests and firmware share one source of truth. */
#include "../../firmware/components/emu8086/include/emu8086.h"

/* The mem array itself is declared in 8086tiny.c. */
extern unsigned char mem[];

/* Test harness primitives. */

/* Load the upstream 8086tiny BIOS blob at REGS_BASE+0x100 (where the
 * decoder lookup tables live). Returns 0 on success, non-zero error.
 * Looks for ../../third_party/8086tiny/bios.bin relative to cwd. */
int t_load_bios(void);

/* Copy `n` bytes into emu mem at the given seg:off. */
void t_load(uint16_t seg, uint16_t off, const void *bytes, size_t n);

/* Print a one-line summary of emulator state. */
void t_dump_state(const char *label);

/* Used by t_assert macros below. Returns 0 on pass, 1 on fail. */
int  t_check(const char *expr, int cond, const char *file, int line);

#define T_EXPECT(cond)        t_check(#cond, (cond), __FILE__, __LINE__)
#define T_EXPECT_EQ(a, b)     t_check(#a " == " #b, (a) == (b), __FILE__, __LINE__)

/* BIOS-call hook. The default `bios_handle_call` in test_helpers.c
 * dispatches to whatever callback the current test registered, or
 * logs and returns if none is set. Tests that exercise specific
 * BIOSSEG entry points (BIOSOUT, BIOSIN, BIOSREAD, ...) install a
 * callback that does the real work. */
typedef void (*t_bios_cb)(unsigned short ip);
void t_set_bios_callback(t_bios_cb cb);
