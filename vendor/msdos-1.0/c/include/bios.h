/*
 * bios.h — BIOS entry-point definitions and virtual-call table.
 *
 * ASM source: 86DOS.asm lines 130-143
 *
 * In the original, the BIOS lives at segment 40H (physical 0x0400).
 * Each "entry point" is a 3-byte far-jump stub:
 *
 *     BIOSSEG EQU 40H
 *     ORG 0
 *           DS 3          ; (padding / vector 0)
 *   BIOSSTAT: DS 3        ; console status
 *   BIOSIN:   DS 3        ; console input
 *   BIOSOUT:  DS 3        ; console output
 *   BIOSPRINT:DS 3        ; printer output
 *   BIOSAUXIN:DS 3        ; aux/serial input
 *   BIOSAUXOUT:DS 3       ; aux/serial output
 *   BIOSREAD: DS 3        ; disk read
 *   BIOSWRITE:DS 3        ; disk write
 *   BIOSDSKCHG:DS 3       ; disk-change detect
 *
 * DOS calls these with  CALL label,BIOSSEG  (a far call into the BIOS
 * segment).  In the C translation we replace the far-call mechanism with
 * a function-pointer vtable so the core logic is testable without real
 * hardware.
 *
 * BIOS calling conventions (inferred from use in 86DOS.asm):
 *
 *   biosstat()     returns: AL=0 no char ready, AL!=0 char ready
 *   biosin()       returns: AL = character
 *   biosout(al)    outputs character AL to console
 *   biosprint(al)  outputs character AL to printer
 *   biosauxin()    returns: AL = character from aux port
 *   biosauxout(al) sends character AL to aux port
 *   biosread(al, bx, cx, dx)
 *                  al=drive, bx=buffer, cx=sector count, dx=start sector
 *                  returns: carry set on error
 *   bioswrite(al, bx, cx, dx)
 *                  same parameters as biosread but writes
 *                  returns: carry set on error
 *   biosdskchg(al) al=drive
 *                  returns: AH<0 if disk changed/unknown,
 *                           AH=1 disk not changed
 */

#ifndef BIOS_H
#define BIOS_H

#include "dos_types.h"

/* -----------------------------------------------------------------------
 * BIOS function pointer types
 * ----------------------------------------------------------------------- */

/* Console status: returns 0 if no character ready, non-zero if ready */
typedef byte (*bios_stat_fn)(void);

/* Console character input: returns character byte */
typedef byte (*bios_in_fn)(void);

/* Console character output */
typedef void (*bios_out_fn)(byte ch);

/* Printer character output */
typedef void (*bios_print_fn)(byte ch);

/* Auxiliary (serial) input */
typedef byte (*bios_auxin_fn)(void);

/* Auxiliary (serial) output */
typedef void (*bios_auxout_fn)(byte ch);

/*
 * Disk read / write.
 * Parameters mirror the 8086 register convention visible in ASM:
 *   drive  = AL = drive number
 *   buf    = BX = buffer address (within DOS segment)
 *   count  = CX = number of sectors
 *   sector = DX = absolute sector number
 * Returns 0 on success, non-zero on error (CF in original).
 *
 * ASM: CALL BIOSREAD,BIOSSEG  at line 1114
 *      CALL BIOSWRITE,BIOSSEG at line 1171
 */
typedef int (*bios_disk_fn)(byte drive, byte *buf, word count, word sector);

/*
 * Disk-change detection.
 * Parameter: drive number (AL).
 * Returns:   AH-equivalent:
 *   positive (>0)  — disk not changed
 *   negative (<0)  — disk changed or unknown
 *
 * ASM: CALL BIOSDSKCHG,BIOSSEG  at line 780
 */
typedef int (*bios_dskchg_fn)(byte drive);

/* -----------------------------------------------------------------------
 * Virtual BIOS table
 * Replaces the far-call stubs at BIOSSEG:BIOSSTAT etc.
 * ----------------------------------------------------------------------- */
typedef struct bios_vtable {
    bios_stat_fn    stat;       /* BIOSSTAT  — console ready?               */
    bios_in_fn      in;         /* BIOSIN    — read console character        */
    bios_out_fn     out;        /* BIOSOUT   — write console character       */
    bios_print_fn   print;      /* BIOSPRINT — write printer character       */
    bios_auxin_fn   auxin;      /* BIOSAUXIN — read aux character            */
    bios_auxout_fn  auxout;     /* BIOSAUXOUT— write aux character           */
    bios_disk_fn    read;       /* BIOSREAD  — read sectors                  */
    bios_disk_fn    write;      /* BIOSWRITE — write sectors                 */
    bios_dskchg_fn  dskchg;     /* BIOSDSKCHG— detect disk change            */
} bios_vtable_t;

/*
 * Global BIOS vtable pointer.  Initialised by dos_init() in init.c and
 * used throughout the kernel.  The original code implicitly uses the
 * hardware at BIOSSEG; we make the dependency explicit here.
 */
extern bios_vtable_t *bios;

/* -----------------------------------------------------------------------
 * Convenience call macros matching the ASM's  CALL BIOSXXX,BIOSSEG  form.
 * These make the translated code read almost like the original mnemonics.
 * ----------------------------------------------------------------------- */
#define BIOSSTAT()          (bios->stat())
#define BIOSIN()            (bios->in())
#define BIOSOUT(ch)         (bios->out(ch))
#define BIOSPRINT(ch)       (bios->print(ch))
#define BIOSAUXIN()         (bios->auxin())
#define BIOSAUXOUT(ch)      (bios->auxout(ch))
#define BIOSREAD(drv,buf,cnt,sec)  (bios->read((drv),(buf),(cnt),(sec)))
#define BIOSWRITE(drv,buf,cnt,sec) (bios->write((drv),(buf),(cnt),(sec)))
#define BIOSDSKCHG(drv)     (bios->dskchg(drv))

#endif /* BIOS_H */
