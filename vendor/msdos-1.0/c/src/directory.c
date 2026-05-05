/*
 * directory.c — Directory search and name parsing routines.
 *
 * Translated from 86DOS.asm.  The following ASM labels are covered here:
 *
 *   GETFILE     86DOS.asm:448-513   — find file in directory by FCB name
 *   IOCHK       86DOS.asm:434-446   — check if name is an I/O device
 *   FINDNAME    86DOS.asm:470-513   — search directory for NAME1 (after MOVNAME)
 *   FILSRCH     86DOS.asm:484-515   — non-device directory search loop
 *   CONTSRCH    86DOS.asm:486-515   — continue search from current LASTENT
 *   GETENTRY    86DOS.asm:516-562   — get next directory entry pointer
 *   NEXTENTRY   86DOS.asm:563-595   — advance to the next directory entry
 *   NONE        86DOS.asm:596-601   — no-more-entries helper (sets carry)
 *   MOVNAME     86DOS.asm:660-695   — copy+upcase FCB name → NAME1, select drive
 *   LODNAME     86DOS.asm:679-695   — copy+upcase 11 bytes from SI → DI
 *   GETBP       86DOS.asm:696-706   — get DPB pointer for given drive number
 *   STARTSRCH   86DOS.asm:764-765   — reset LASTENT and fall into FATREAD
 */

#include <string.h>
#include "../include/dos.h"

/* Forward declaration (fat.c) */
extern void fat_read(byte *bp);

/* -----------------------------------------------------------------------
 * Device name table
 * ASM: IONAME  86DOS.asm:3212
 * Four 3-character device names: "PRN", "LST", "AUX", "CON"
 * ----------------------------------------------------------------------- */
#define IONAME_COUNT  4
static const byte IONAME[IONAME_COUNT][3] = {
    { 'P','R','N' },
    { 'L','S','T' },
    { 'A','U','X' },
    { 'C','O','N' }
};

/* -----------------------------------------------------------------------
 * getbp — Look up the DPB pointer for a drive number.
 *
 * ASM: GETBP  86DOS.asm:696-706
 *
 * Inputs:
 *   drive — drive number (0-based); 0 = drive A
 * Outputs:
 *   Returns pointer to DPB for that drive, or NULL if drive >= NUMDRV.
 *   Carry is set in ASM on invalid drive; here we return NULL.
 *
 * ASM:
 *   SEG CS
 *   CMP [NUMDRV],AL
 *   JC  RET            ; carry set → bad drive
 *   CBW
 *   XCHG BP,AX
 *   SHL  BP            ; BP = drive * 2 (word table)
 *   MOV  BP,[BP+CURDRVPT]
 *   RET
 * ----------------------------------------------------------------------- */
byte *getbp(byte drive)
{
    if (drive >= dos->NUMDRV)
        return NULL;   /* CF set in ASM */
    return dos->DRVTAB[drive];
}

/* -----------------------------------------------------------------------
 * startsrch — Reset directory search state, then re-read FAT if needed.
 *
 * ASM: STARTSRCH  86DOS.asm:764-765  (falls through into FATREAD)
 *
 * In the ASM STARTSRCH sets LASTENT = -1 and then falls through to
 * FATREAD which optionally re-reads the FAT.  In C we call fat_read
 * directly to mirror that behaviour.
 *
 * Inputs:
 *   bp — pointer to current DPB
 * Outputs:
 *   dos->LASTENT = 0xFFFF (treated as -1 in unsigned arithmetic)
 *   FAT re-read if disk may have changed.
 * ----------------------------------------------------------------------- */
void startsrch(byte *bp)
{
    dos->LASTENT = 0xFFFF;   /* MOV [LASTENT],-1 */
    fat_read(bp);            /* FATREAD path */
}

/* -----------------------------------------------------------------------
 * getentry — Locate next sequential directory entry for search.
 *
 * ASM: GETENTRY  86DOS.asm:516-562
 *
 * Inputs:
 *   bp     — pointer to DPB
 * Outputs:
 *   Returns 0 (CF clear) on success:
 *     *bx_out  — pointer into DIRBUF to the directory entry
 *     *al_out  — current directory block number (sector index in dir)
 *     dos->LASTENT is updated to the entry number we are returning.
 *   Returns -1 (CF set) if no more entries (LASTENT+1 >= MAXENT).
 *
 * ASM logic:
 *   AX = LASTENT + 1
 *   if AX >= MAXENT → NONE (carry, write-back dir if dirty)
 *   LASTENT = AX
 *   AX <<= 4         (entry_offset = entry_no * 16 bytes, small entries)
 *   if !SMALLDIR: AX <<= 1 (entry_offset = entry_no * 32 bytes)
 *   secsiz = SECSIZ & ~31  (must be multiple of 32)
 *   AL = AX / secsiz  → directory sector index
 *   BX = AX % secsiz  → byte offset within sector
 *   AH = DRVNUM       → form AX = drive:sector id
 *   if AX != DIRBUFID → DIRREAD to load the right sector
 *   BX += DIRBUF
 *   DX  = DIRBUF + SECSIZ  (end-of-buffer sentinel)
 * ----------------------------------------------------------------------- */
int getentry(byte *bp, byte **bx_out, byte *al_out)
{
    word maxent  = DPB_GET_WORD(bp, MAXENT);
    word secsiz  = DPB_GET_WORD(bp, SECSIZ);
    byte drvnum  = DPB_GET_BYTE(bp, DRVNUM);
    byte smalldir = DPB_GET_BYTE(bp, DIRSIZ);  /* DIRSIZ alias: -1 = small */

    word entry, ax;
    word secsiz_aligned;
    byte dir_sector;

    /* AX = LASTENT + 1 */
    entry = (word)(dos->LASTENT + 1u);
    if (entry >= maxent) {
        /* NONE path */
        chkdirwrite(bp);
        return -1;   /* CF set */
    }
    dos->LASTENT = entry;

    /* entry_offset = entry * 16; double if large (32-byte) entries */
    ax = (word)(entry << 4);

    if (smalldir != 0xFF) {
        /* Large (32-byte) directory entries: shift left one more bit.
         * ASM: SHL AX / RCL DX  (accounts for 16-bit overflow)
         * NOTE: dx_rem (high-word of 32-bit offset) omitted — in a real
         * 16-bit system it matters but practical directories fit 64KB.  */
        ax = (word)(ax << 1);
    }

    /* secsiz must be a multiple of 32 */
    secsiz_aligned = secsiz & (word)(~31u);
    if (secsiz_aligned == 0) secsiz_aligned = 32; /* safety */

    /* AL = ax / secsiz_aligned, BX = ax % secsiz_aligned */
    dir_sector = (byte)(ax / secsiz_aligned);
    ax         = ax % secsiz_aligned;   /* reuse ax as the offset */

    {
        word dirbufid_cmp = ((word)drvnum << 8) | (word)dir_sector;
        if (dirbufid_cmp != dos->DIRBUFID) {
            dirread(dir_sector, bp);
        }
    }

    *bx_out = dos->DIRBUF + ax;                 /* pointer into buffer */
    /* DX = DIRBUF + SECSIZ — caller uses as end-of-buffer limit */
    /* We return it implicitly; callers compute it from DIRBUF+SECSIZ    */
    if (al_out) *al_out = dir_sector;
    return 0;   /* CF clear */
}

/* -----------------------------------------------------------------------
 * nextentry — Advance BX to the next directory entry.
 *
 * ASM: NEXTENTRY  86DOS.asm:563-595
 *
 * Inputs:
 *   bp       — pointer to DPB
 *   bx_inout — current pointer into DIRBUF; updated on return
 *   dx_limit — pointer one past the end of DIRBUF (DIRBUF + SECSIZ)
 *   al_io    — current directory sector index; updated on return
 * Outputs:
 *   Returns 0 (CF clear) if a next entry exists.
 *   Returns -1 (CF set) if no more entries.
 *   *bx_inout and *al_io are updated.
 *
 * ASM:
 *   DI = LASTENT + 1
 *   if DI >= MAXENT → NONE
 *   LASTENT = DI
 *   BX += 32  (or 16 if SMALLDIR)
 *   if BX >= DX → read next directory sector, BX = DIRBUF
 * ----------------------------------------------------------------------- */
int nextentry(byte *bp, byte **bx_inout, byte *dx_limit, byte *al_io)
{
    word maxent  = DPB_GET_WORD(bp, MAXENT);
    byte smalldir = DPB_GET_BYTE(bp, DIRSIZ);
    word entry_size = (smalldir == 0xFF) ? 16u : 32u;

    word di = (word)(dos->LASTENT + 1u);
    if (di >= maxent) {
        chkdirwrite(bp);
        return -1;   /* CF set / NONE */
    }
    dos->LASTENT = di;

    *bx_inout += entry_size;

    if (*bx_inout >= dx_limit) {
        /* Need next directory sector */
        (*al_io)++;
        dirread(*al_io, bp);
        *bx_inout = dos->DIRBUF;
    }
    return 0;   /* CF clear */
}

/* -----------------------------------------------------------------------
 * lodname — Copy 11-byte file name from src to dst, converting to upper case.
 *
 * ASM: LODNAME  86DOS.asm:679-695
 *
 * Inputs:
 *   src — pointer to 11-byte name (from FCB or second-name field)
 *   dst — destination (e.g. dos->NAME2)
 * Outputs:
 *   Copies 11 bytes, masking bit 7 and upcasing a-z.
 *   Returns normally (no carry in ASM from this path; control characters
 *   below 0x20 cause an early return in MOVCHK, but here we guard with
 *   a simple check and stop — caller is responsible for valid input).
 *
 * ASM (MOVCHK loop):
 *   LODB        ; AL = *SI++
 *   AND AL,7FH  ; strip bit 7
 *   CMP AL,60H
 *   JLE CASEOK
 *   AND AL,5FH  ; to upper case
 *   CASEOK:
 *   CMP AL,20H
 *   JC  RET     ; control char → abort
 *   STOB        ; *DI++ = AL
 *   LOOP MOVCHK
 * ----------------------------------------------------------------------- */
void lodname(byte *src, byte *dst)
{
    int i;
    for (i = 0; i < 11; i++) {
        byte al = src[i] & 0x7F;
        if (al > 0x60) al &= 0x5F;   /* to upper case */
        if (al < 0x20) break;         /* control character → stop */
        dst[i] = al;
    }
}

/* -----------------------------------------------------------------------
 * movname — Parse FCB pointed to by fcb_ptr; fill NAME1; select drive.
 *
 * ASM: MOVNAME  86DOS.asm:660-695
 *
 * Inputs:
 *   fcb_ptr — pointer to user FCB (DS:DX in ASM; first byte is drive)
 * Outputs:
 *   Returns pointer to the DPB for the drive in the FCB, or NULL on error.
 *   Fills dos->NAME1[0..10] with the 11-byte name from the FCB.
 *   Carry set → return NULL (bad file name or bad drive number).
 *
 * ASM:
 *   ES = CS
 *   DI = NAME1
 *   SI = DX
 *   AL = [SI] (drive byte, 0 = current)
 *   CALL GETBP   ; get DPB pointer into BP
 *   JB   RET     ; bad drive → carry, return
 *   (fall through to LODNAME to copy name)
 * ----------------------------------------------------------------------- */
int movname(byte *fcb_ptr, byte **bp_out)
{
    byte drive = fcb_ptr[0];   /* first byte of FCB is drive (0 = current) */
    byte *bp;

    /* Drive 0 means "current drive" */
    if (drive == 0) {
        bp = dos->CURDRVPT;
    } else {
        bp = getbp((byte)(drive - 1));
    }
    if (bp == NULL) {
        return -1;   /* CF set */
    }
    *bp_out = bp;

    /* Copy name bytes 1-11 from FCB into NAME1 */
    lodname(fcb_ptr + 1, dos->NAME1);
    return 0;   /* CF clear */
}

/* -----------------------------------------------------------------------
 * findname — Search directory for dos->NAME1 after drive/name are set up.
 *
 * ASM: FINDNAME  86DOS.asm:470-515
 *
 * This is the entry point used after MOVNAME has already been called and
 * the name may or may not be a device.  It first scans the IONAME table.
 * If matched it returns the device indicator.  Otherwise it searches the
 * directory on disk.
 *
 * Outputs:
 *   Returns 0 (CF clear) on success:
 *     *bx_out  — pointer into DIRBUF to start of matching entry
 *     *si_out  — pointer to First Cluster field (bx + 15 for large dir,
 *                bx + 7 for small dir — ASM line 512: ADD SI,15)
 *     *bp_out  — pointer to DPB (drive parameters)
 *   Returns -1 (CF set) if not found.
 *   If device: *bx_out has high byte = 0xFF (BH=-1 in ASM).
 *
 * ASM device check (LOOKIO loop):
 *   SI = IONAME
 *   BL = 4 (number of devices)
 *   DI = NAME1
 *   CX = 3
 *   REPE CMPB        ; compare first 3 bytes
 *   JZ IOCHK         ; first 3 match → check rest is spaces
 *   ADD SI,CX        ; skip remaining name bytes
 *   DEC BL
 *   JNZ LOOKIO
 *
 * IOCHK (434-446):
 *   CX = 5
 *   if [DI] == ':' → INC DI, DEC CX
 *   AL = ' '
 *   REPE SCAB        ; scan rest for non-blank
 *   JNZ FILSRCH      ; non-blank byte → not a device, search disk
 *   DEC BL           ; BL will be 0xFF after this if was 1, or device index
 *   RET              ; return with BH=-1 flag... actually BH is untouched
 *
 * NOTE: In ASM BH is set to -1 via BX=0xFF04H initial value (BH=0xFF,
 *       BL=4). When IOCHK succeeds it just RETurns; the caller checks
 *       BH == -1 to detect device.  We set bx_out high byte accordingly.
 * ----------------------------------------------------------------------- */
int findname(byte **bx_out, byte **si_out, byte **bp_out)
{
    int i;

    /* Scan I/O device name table — LOOKIO loop */
    for (i = 0; i < IONAME_COUNT; i++) {
        /* Compare first 3 characters */
        if (dos->NAME1[0] == IONAME[i][0] &&
            dos->NAME1[1] == IONAME[i][1] &&
            dos->NAME1[2] == IONAME[i][2]) {
            /* IOCHK: remaining 5 chars (bytes 3-7) must all be spaces or
             * optionally preceded by a colon.                             */
            int j    = 3;
            int limit = 8;   /* 3 + 5 = 8 */
            if (dos->NAME1[3] == ':') j++;  /* skip colon */
            {
                int ok = 1;
                int k;
                for (k = j; k < limit && k < 11; k++) {
                    if (dos->NAME1[k] != ' ') { ok = 0; break; }
                }
                if (ok) {
                    /* Device match: set BH = -1 flag via high byte trick */
                    /* In the C model we use a static sentinel byte array */
                    static byte dev_sentinel[2] = { 0x00, 0xFF }; /* BL, BH */
                    /* bx_out high byte = 0xFF signals device to caller    */
                    *bx_out = dev_sentinel; /* BH = 0xFF */
                    *si_out = NULL;
                    /* bp_out unchanged — *bp_out must already be set by
                     * the caller (movname sets it before calling us)      */
                    return 0;
                }
            }
            /* else: non-blank in rest → fall through to FILSRCH */
        }
    }

    /* FILSRCH: search the disk directory */
    return contsrch(bx_out, si_out, *bp_out);
}

/* -----------------------------------------------------------------------
 * contsrch — Continue (or start) directory search from dos->LASTENT.
 *
 * ASM: CONTSRCH / FILSRCH  86DOS.asm:484-515
 *
 * FILSRCH calls STARTSRCH first, then falls into CONTSRCH.
 * CONTSRCH is re-entered from DELETE and RENAME to continue a search.
 *
 * Inputs:
 *   bp — pointer to DPB for the drive to search
 * Outputs:
 *   Returns 0 (CF clear) on success:
 *     *bx_out — pointer into DIRBUF to start of matching entry
 *     *si_out — pointer to First Cluster field (bx + 15, or bx + 7 small)
 *   Returns -1 (CF set) if not found.
 *
 * ASM (CONTSRCH → GETENTRY → SRCH loop):
 *   CONTSRCH:
 *     CALL GETENTRY
 *     JC   RET             ; no more entries
 *   SRCH:
 *     CMP B,[BX],0E5H      ; deleted entry?
 *     JZ  NEXTENT
 *     SI = BX
 *     DI = NAME1
 *     CX = 11
 *   WILDCRD:
 *     REPE CMPB            ; compare name bytes
 *     JZ   FOUND
 *     CMP B,[DI-1],"?"     ; wildcard?
 *     JZ  WILDCRD          ; yes, keep comparing
 *   NEXTENT:
 *     CALL NEXTENTRY
 *     JNC  SRCH
 *     RET                  ; CF set
 *   FOUND:
 *     IF SMALLDIR
 *       CMP B,[BP+DIRSIZ],-1
 *       JZ  RET            ; small-dir: DIRSIZ==-1 means already at SI
 *     ENDIF
 *     ADD SI,15
 *     RET
 * ----------------------------------------------------------------------- */
int contsrch(byte **bx_out, byte **si_out, byte *bp)
{
    byte *bx;
    byte al_sector = 0;
    byte smalldir  = DPB_GET_BYTE(bp, DIRSIZ);
    word secsiz    = DPB_GET_WORD(bp, SECSIZ);
    byte *dx_limit = dos->DIRBUF + secsiz;
    int   rc;

    rc = getentry(bp, &bx, &al_sector);
    if (rc != 0)
        return -1;   /* CF set */

    for (;;) {
        /* SRCH: check for deleted entry marker */
        if (bx[0] == 0xE5u) {
            goto next_entry;
        }
        /* End-of-directory terminator: a zero first byte means this entry
         * (and all that follow) are unused.  The original DOS 1.0 ASM did
         * not test for this — FORMAT pre-filled directory slots with 0xE5
         * — but a freshly-zeroed volume image (and DOS 2.0+ convention)
         * relies on 0x00 acting as end-of-dir.  Without this guard a
         * wildcard search ('?') matches the all-zero slot and we would
         * report a phantom file. */
        if (bx[0] == 0x00u) {
            return -1;   /* CF set: no more entries */
        }

        /* Compare 11-byte name with NAME1, respecting '?' wildcard */
        {
            int matched = 1;
            int j;
            for (j = 0; j < 11; j++) {
                if (bx[j] != dos->NAME1[j] && dos->NAME1[j] != '?') {
                    matched = 0;
                    break;
                }
            }
            if (matched) {
                /* FOUND */
                if (smalldir == 0xFF) {
                    /* SMALLDIR: carry set means DIRSIZ == -1 which in ASM
                     * means small-dir entries.  ASM line 508: JZ RET
                     * (returns immediately without ADD SI,15).
                     * NOTE: the ASM comment is slightly confusing — when
                     * DIRSIZ field == -1 the dir IS small.  We return
                     * bx as-is for small entries.                         */
                    *bx_out = bx;
                    *si_out = bx;  /* SI = BX; no +15 offset for small */
                    return 0;
                }
                /* Large (32-byte) entries: first cluster is at offset 15 */
                *bx_out = bx;
                *si_out = bx + 15;   /* ADD SI,15 */
                return 0;
            }
        }

    next_entry:
        rc = nextentry(bp, &bx, dx_limit, &al_sector);
        /* Recompute dx_limit after possible sector reload */
        dx_limit = dos->DIRBUF + secsiz;
        if (rc != 0)
            return -1;   /* CF set */
    }
}

/* -----------------------------------------------------------------------
 * getfile — Find a file in the directory by FCB name.
 *
 * ASM: GETFILE  86DOS.asm:448-515
 *
 * Inputs:
 *   fcb_ptr — pointer to user FCB (first byte = drive, next 11 = name)
 * Outputs:
 *   Returns 0 (CF clear) on success:
 *     *bx_out  — pointer into DIRBUF to matching directory entry
 *     *si_out  — pointer to First Cluster field in that entry
 *     *bp_out  — pointer to the DPB (drive parameters base)
 *     dos->NAME1 is filled with the parsed file name
 *     dos->DIRBUF contains the relevant directory sector
 *   Returns -1 (CF set) if file not found or bad drive/name.
 *
 * ASM:
 *   CALL MOVNAME     ; parse name, validate drive, set BP
 *   JC   RET         ; bad name/drive
 *   (fall through to FINDNAME)
 * ----------------------------------------------------------------------- */
int getfile(byte *fcb_ptr, byte **bx_out, byte **si_out, byte **bp_out)
{
    byte *bp = NULL;

    if (movname(fcb_ptr, &bp) != 0)
        return -1;   /* CF set */

    /* Start from beginning of directory */
    startsrch(bp);

    *bp_out = bp;
    return findname(bx_out, si_out, bp_out);
}
