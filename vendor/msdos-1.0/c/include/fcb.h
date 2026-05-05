/*
 * fcb.h — File Control Block field offsets and structure definition.
 *
 * ASM source: 86DOS.asm lines 58-73  (ORG 0 … NR/RR fields)
 *
 * The FCB is the primary file handle used by all 86-DOS file functions.
 * Its layout is fixed — user programs address fields by numeric offset —
 * so we define both a struct and the raw byte offsets used in the ASM.
 *
 * The original ASM defines the offsets with an ORG 0 / DS sequence:
 *
 *     ORG   0
 *     DS    12       ;Drive code and name
 *   EXTENT: DS 2
 *   RECSIZ: DS 2     ;Size of record (user settable)
 *   FILSIZ: DS 4     ;Size of file in bytes
 *   FDATE:  DS 2     ;Date of last writing
 *   FILDIRBLK: DS 2  ;Location in directory
 *   FIRCLUS: DS 2    ;First cluster of file
 *   LSTCLUS: DS 2    ;Last cluster accessed
 *   CLUSPOS: DS 2    ;Position of last cluster accessed
 *   DIRTYFIL: DS 1   ;File has been written to if <>0
 *     ORG   32
 *   NR:     DS 1     ;Next record
 *   RR:     DS 3     ;Random record
 */

#ifndef FCB_H
#define FCB_H

#include "dos_types.h"

/* -----------------------------------------------------------------------
 * Field byte offsets within the FCB  (86DOS.asm lines 60-73)
 * These match the ASM equates exactly (same names, upper-case).
 * ----------------------------------------------------------------------- */
#define FCB_DRIVE       0       /* 1 byte  — drive number (0=current)       */
#define FCB_NAME        1       /* 8 bytes — filename, space-padded          */
#define FCB_EXT         9       /* 3 bytes — extension, space-padded         */
/* offset 12 = start of "system" fields (below) */
#define EXTENT          12      /* 2 bytes — extent number                   */
#define RECSIZ          14      /* 2 bytes — logical record size             */
#define FILSIZ          16      /* 4 bytes — file size in bytes (32-bit)     */
#define FDATE           20      /* 2 bytes — date of last write              */
#define FILDIRBLK       22      /* 2 bytes — directory entry number          */
#define FIRCLUS         24      /* 2 bytes — first cluster of file           */
#define LSTCLUS         26      /* 2 bytes — last cluster accessed           */
#define CLUSPOS         28      /* 2 bytes — position of LSTCLUS in chain    */
#define DIRTYFIL        30      /* 1 byte  — non-zero if written             */
/* byte 31 is padding/reserved */
#define NR              32      /* 1 byte  — next sequential record number   */
#define RR              33      /* 3 bytes — random record number (24-bit)   */
/* byte 36 = total FCB size for large (32-byte dir) format                  */

#define FCB_SIZE        36      /* Total size of an opened FCB               */
#define FCB_NAME_LEN    11      /* 8 + 3 characters                          */

/*
 * Special value stored in FILDIRBLK when the FCB refers to a named I/O
 * device (CON, PRN, AUX, LST) rather than a disk file.
 * The high byte BH=-1 (0xFF) is tested with  CMP BH,-1  in the ASM.
 *   ASM: 86DOS.asm lines 721, 848, 1000, 1722, 1907, 2312, 2409
 */
#define FCB_DEVICE_FLAG 0xFF00  /* FILDIRBLK value meaning "I/O device"      */

/* -----------------------------------------------------------------------
 * C struct mirror of the FCB
 *
 * NOTE: The struct is provided for readability. The kernel internally
 * accesses FCBs through byte-offset macros (above) applied to a byte*
 * pointer, exactly mirroring the ASM's  MOV AX,[DI+RECSIZ]  style.
 * ----------------------------------------------------------------------- */
typedef struct fcb {
    byte  drive;            /* [0]    drive number (0 = current drive)      */
    byte  name[8];          /* [1-8]  filename, space padded                */
    byte  ext[3];           /* [9-11] extension, space padded               */
    word  extent;           /* [12]   EXTENT                                */
    word  recsiz;           /* [14]   RECSIZ — record size                  */
    dword filsiz;           /* [16]   FILSIZ — file size in bytes           */
    word  fdate;            /* [20]   FDATE  — date of last write           */
    word  fildirblk;        /* [22]   FILDIRBLK — directory entry number    */
    word  firclus;          /* [24]   FIRCLUS — first cluster               */
    word  lstclus;          /* [26]   LSTCLUS — last cluster accessed       */
    word  cluspos;          /* [28]   CLUSPOS — position of LSTCLUS         */
    byte  dirtyfil;         /* [30]   DIRTYFIL — written flag               */
    byte  _pad;             /* [31]   (unused)                              */
    byte  nr;               /* [32]   NR — next record                      */
    byte  rr[3];            /* [33]   RR — random record (24-bit LE)        */
    /* The RR field can also be treated as 4 bytes when the high byte is
     * significant (very large files).  See FINRND, 86DOS.asm line 1302. */
} fcb_t;

/* -----------------------------------------------------------------------
 * Inline accessors matching the ASM's byte-pointer access style.
 * p must be a  byte *  pointing to the start of the FCB.
 * ----------------------------------------------------------------------- */
#define FCB_GET_BYTE(p, off)    (*((p) + (off)))
#define FCB_GET_WORD(p, off)    (*(word *)((p) + (off)))
#define FCB_GET_DWORD(p, off)   (*(dword *)((p) + (off)))
#define FCB_SET_WORD(p, off, v) (*(word *)((p) + (off)) = (word)(v))
#define FCB_SET_BYTE(p, off, v) (*((p) + (off)) = (byte)(v))

#endif /* FCB_H */
