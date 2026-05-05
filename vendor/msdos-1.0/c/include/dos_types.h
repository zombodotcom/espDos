/*
 * dos_types.h — Fundamental types and constants for the 86-DOS C translation.
 *
 * ASM source: 86DOS.asm (constants defined at lines 44-56, 132, etc.)
 *
 * The original code runs on the 8086 in real mode with 16-bit registers.
 * Every integer type in the translation uses the explicit-width aliases
 * defined here so the 16-bit nature of the original is always visible.
 *
 * Segment:offset addressing cannot be expressed in portable C.  Where the
 * ASM uses a segment register (CS, DS, ES) to qualify a memory access the
 * C code uses a plain pointer and marks the access with a comment of the
 * form  [SEG CS]  so it can be found easily during review.
 */

#ifndef DOS_TYPES_H
#define DOS_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Basic width aliases
 * ----------------------------------------------------------------------- */
typedef uint8_t   byte;          /* 8-bit unsigned  — "B" in ASM operands  */
typedef uint16_t  word;          /* 16-bit unsigned — default register size */
typedef uint32_t  dword;         /* 32-bit (pair of registers, DX:AX etc.)  */
typedef int8_t    sbyte;
typedef int16_t   sword;

/* -----------------------------------------------------------------------
 * Assembly-level equates  (86DOS.asm lines 44-56)
 * ----------------------------------------------------------------------- */
#define MAXCALL     36          /* Highest function number via CALL 5       */
#define MAXCOM      41          /* Highest function number via INT 21H      */
#define ESCCH       0x1B        /* Escape character                         */
#define INTBASE     0x80        /* Base of DOS interrupt vectors             */
#define INTTAB      0x20        /* INT 20H — abort                          */
#define ENTRYPOINTSEG 0x0C      /* Far-call segment adjustment              */
#define ENTRYPOINT  (INTBASE + 0x40)  /* Long-jump stub address             */
#define CONTC       (INTTAB + 3)      /* Ctrl-C interrupt vector            */
#define EXIT        (INTBASE + 8)     /* Exit address in vector table       */
#define LONGJUMP    0xEA        /* Far-jump opcode                          */
#define LONGCALL    0x9A        /* Far-call opcode                          */
#define MAXDIF      0x0FFF      /* Maximum segment difference               */
#define SAVEXIT     10          /* Offset of saved exit address in PSP      */

/* -----------------------------------------------------------------------
 * BIOS segment value  (86DOS.asm line 132)
 * The BIOS entry stubs live at paragraph 40H (physical 0x0400).
 * In the C translation these are replaced by function pointers in
 * bios_vtable_t (see bios.h).
 * ----------------------------------------------------------------------- */
#define BIOSSEG     0x40

/* -----------------------------------------------------------------------
 * Compile-time feature flags  (86DOS.asm lines 22, 29)
 * ----------------------------------------------------------------------- */
#define SMALLDIR    1           /* 1 = accept old 16-byte directory entries */
#define DSKTEST     0           /* 1 = separate disk-I/O stack for DEBUG    */

/* -----------------------------------------------------------------------
 * FAT12 special cluster values  (86DOS.asm lines 89-98)
 * ----------------------------------------------------------------------- */
#define FAT_FREE    0x000       /* Unallocated cluster                      */
#define FAT_EOF_MIN 0xFF8       /* Lowest end-of-file marker value          */
#define FAT_EOF     0xFFF       /* Standard end-of-file marker              */
#define FAT_BAD     0xFF7       /* Bad cluster marker                       */

/* -----------------------------------------------------------------------
 * Helpers for 32-bit DX:AX pairs
 * The 8086 frequently uses the DX register as the high word and AX as
 * the low word of a 32-bit value.  These macros make such pairs explicit.
 * ----------------------------------------------------------------------- */
#define MAKE32(hi, lo)  (((dword)(hi) << 16) | (word)(lo))
#define HI16(v)         ((word)((dword)(v) >> 16))
#define LO16(v)         ((word)((dword)(v) & 0xFFFF))

/* -----------------------------------------------------------------------
 * Carry-flag emulation
 * Many ASM routines signal success/failure through the carry flag.
 * In C we use int return values:  0 = success (CF=0), -1 = error (CF=1).
 * ----------------------------------------------------------------------- */
#define CF_CLEAR    0
#define CF_SET      (-1)

#endif /* DOS_TYPES_H */
