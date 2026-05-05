/*
 * dpb.h — Drive Parameter Block field offsets and structure definition.
 *
 * ASM source: 86DOS.asm lines 101-127
 *
 * One DPB is allocated per logical drive during DOSINIT.  The FAT image
 * for the drive is stored immediately after the last field of the DPB
 * (at label FAT:, ASM line 126) — hence the flexible-array member below.
 *
 * Original ASM layout (ORG 0 / DS sequence):
 *
 *   DRVNUM:   DS 1     Drive number
 *   SECSIZ:   DS 2     Size of physical sector in bytes
 *   CLUSMSK:  DS 1     Sectors/cluster - 1
 *   CLUSSHFT: DS 1     Log2 of sectors/cluster
 *   FIRFAT:   DS 2     Starting record of FATs
 *   FATCNT:   DS 1     Number of FATs for this drive
 *   MAXENT:   DS 2     Number of directory entries
 *   DIRSEC:            Number of dir. sectors (init temporary, same offset as FIRREC)
 *   FIRREC:   DS 2     First sector of first cluster
 *   DSKSIZ:            Size of disk (temp during init, same offset as MAXCLUS)
 *   MAXCLUS:  DS 2     Number of clusters on drive + 1
 *   FATSIZ:   DS 1     Number of records occupied by FAT
 *   FIRDIR:   DS 2     Starting record of directory
 *   [IF SMALLDIR]
 *   FIRREC1:  DS 2     First data sector with 16-byte dir. entries
 *   MAXCLUS1: DS 2     No. of clusters + 1 with 16-byte dir. entries
 *   FIRREC2:  DS 2     First data sector with 32-byte dir. entries
 *   MAXCLUS2: DS 2     No. of clusters + 1 with 32-byte dir entries
 *   [ENDIF]
 *   DIRTYFAT: DS 1     1=FAT has been changed, -1=never been read
 *   FAT:               Start of FAT (immediately follows DPB)
 *   DIRSIZ:            -1=small dir. entry, else large  (aliases FAT byte 0)
 */

#ifndef DPB_H
#define DPB_H

#include "dos_types.h"

/* -----------------------------------------------------------------------
 * Byte offsets within the DPB  (86DOS.asm lines 104-127)
 * Same names and case as the ASM equates.
 * ----------------------------------------------------------------------- */
#define DRVNUM      0   /* byte  — drive number (0-based)                   */
#define SECSIZ      1   /* word  — physical sector size in bytes            */
#define CLUSMSK     3   /* byte  — sectors/cluster minus 1                  */
#define CLUSSHFT    4   /* byte  — log2(sectors/cluster)                    */
#define FIRFAT      5   /* word  — first sector of FAT area                 */
#define FATCNT      7   /* byte  — number of FAT copies                     */
#define MAXENT      8   /* word  — number of directory entries              */
#define FIRREC      10  /* word  — first data sector (= DIRSEC during init) */
#define MAXCLUS     12  /* word  — total clusters + 1 (= DSKSIZ during init)*/
#define FATSIZ      14  /* byte  — sectors per FAT copy                     */
#define FIRDIR      15  /* word  — first directory sector                   */

/* SMALLDIR extended fields (86DOS.asm lines 119-123) */
#define FIRREC1     17  /* word  — first data sector (16-byte dir entries)  */
#define MAXCLUS1    19  /* word  — clusters+1 (16-byte dir entries)         */
#define FIRREC2     21  /* word  — first data sector (32-byte dir entries)  */
#define MAXCLUS2    23  /* word  — clusters+1 (32-byte dir entries)         */

/*
 * DIRTYFAT offset depends on whether SMALLDIR fields are present.
 * 86DOS.asm line 125.
 */
#if SMALLDIR
#define DIRTYFAT    25  /* byte  — FAT dirty flag (SMALLDIR build)          */
#define DPB_FIXED_SIZE 26
#else
#define DIRTYFAT    17  /* byte  — FAT dirty flag (no-SMALLDIR build)       */
#define DPB_FIXED_SIZE 18
#endif

/*
 * FAT image begins at DPB_FIXED_SIZE (the byte immediately after DIRTYFAT).
 * The label FAT: in the ASM falls here (86DOS.asm line 126).
 *
 * DIRSIZ occupies the same byte as FAT[0] (86DOS.asm line 127):
 *   0xFF (-1 as signed byte) => 16-byte (small) directory entries
 *   0x00                     => 32-byte (large) directory entries
 */
#define FAT_OFFSET      DPB_FIXED_SIZE
#define DIRSIZ          FAT_OFFSET   /* aliases first byte of FAT area       */

/* -----------------------------------------------------------------------
 * DIRTYFAT special values  (86DOS.asm line 125, FATREAD logic ~line 783)
 * ----------------------------------------------------------------------- */
#define DIRTYFAT_CLEAN  0x00    /* FAT matches disk                         */
#define DIRTYFAT_DIRTY  0x01    /* FAT has unsaved changes                  */
#define DIRTYFAT_UNREAD 0xFF    /* FAT has never been read (-1 as byte)     */

/* -----------------------------------------------------------------------
 * Inline accessor macros for DPB accessed via raw byte pointer  bp
 * (mirroring  [BP+SECSIZ]  etc. in the ASM).
 *
 * These are the primary access method in the translated code.
 * ----------------------------------------------------------------------- */
#define DPB_GET_BYTE(bp, off)       (*((byte *)(bp) + (off)))
#define DPB_GET_WORD(bp, off)       (*(word *)((byte *)(bp) + (off)))
#define DPB_SET_BYTE(bp, off, v)    (*((byte *)(bp) + (off)) = (byte)(v))
#define DPB_SET_WORD(bp, off, v)    (*(word *)((byte *)(bp) + (off)) = (word)(v))

/* Pointer to the in-memory FAT given a DPB base pointer */
#define DPB_FAT_PTR(bp)  ((byte *)(bp) + FAT_OFFSET)

#endif /* DPB_H */
