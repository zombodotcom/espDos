#pragma once

#include <stddef.h>
#include <stdint.h>

/* Public interface from 8086tiny.c */
extern unsigned char mem[];
extern int           emu_run_n(int max_steps);
extern void          emu_init_state(void);
extern void          emu_load_bios_tables(void);
extern void          emu_set_cs_ip(unsigned short cs, unsigned short ip);
extern unsigned short emu_get_cs(void);
extern unsigned short emu_get_ip(void);
extern unsigned short emu_get_ax(void);

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
