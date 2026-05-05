/*
 * disk.c — Low-level sector I/O, sector buffer, and directory buffer.
 *
 * Translated from 86DOS.asm.  ASM labels covered:
 *
 *   DREAD       86DOS.asm:1095-1149  — read sectors via BIOS
 *   HARDREAD    86DOS.asm:1123-1149  — read error handler
 *   DWRITE      86DOS.asm:1150-1183  — write sectors via BIOS
 *   HARDWRITE   86DOS.asm:1180-1183  — write error handler
 *   HARDERR     86DOS.asm:1186-1214  — generic hard-error prompt (A/R/I/C)
 *   DIRREAD     86DOS.asm:1071-1094  — read directory sector into DIRBUF
 *   DIRWRITE    86DOS.asm:1133-1149  — write DIRBUF back to disk
 *   CHKDIRWRITE 86DOS.asm:1129-1132  — conditional DIRWRITE
 *   DIRCOMP     86DOS.asm:963-972    — compute DX/BX/CX for directory I/O
 *   BUFSEC      86DOS.asm:1508-1566  — ensure a sector is in the buffer
 *   BUFRD       86DOS.asm:1567-1580  — copy buffer → DMA area
 *   BUFWRT      86DOS.asm:1581-1607  — copy DMA area → buffer
 *   NEXTSEC     86DOS.asm:1608-1706  — advance to next sector in file
 */

#include <string.h>
#include "../include/dos.h"

/* Forward declaration of the internal error-recovery helper */
static int harderr_prompt(const byte *msg, byte drive,
                          byte **buf_io, word *cx_io, word *dx_io,
                          byte *bp);


/* -----------------------------------------------------------------------
 * dread — Read sectors from disk via BIOS.
 *
 * ASM: DREAD  86DOS.asm:1095-1149
 *
 * Inputs:
 *   buf    — transfer buffer
 *   count  — number of sectors (CX)
 *   sector — absolute sector number (DX)
 *   bp     — DPB pointer (for drive number and error recovery)
 * Outputs:
 *   Returns 0 on success, non-zero on unrecoverable error.
 *
 * On BIOS error the user is prompted "Abort / Retry / Ignore / Continue"
 * (HARDERR, ASM lines 1186-1214).  This loop corresponds to the
 * JP DREAD at ASM line 1126.
 * ----------------------------------------------------------------------- */
int dread(byte *buf, word count, word sector, byte *bp)
{
    byte drive = DPB_GET_BYTE(bp, DRVNUM);

    for (;;) {
        int err = BIOSREAD(drive, buf, count, sector);
        if (!err) return 0;

        /* HARDREAD: prompt user */
        {
            int action = harderr_prompt(
                (const byte *)"\r\nDisk read error$",
                drive, &buf, &count, &sector, bp);
            if (action == 0) return 0;   /* Ignore */
            if (action == 1) return 1;   /* Continue (report error) */
            /* action == 2: Retry — loop */
        }
    }
}


/* -----------------------------------------------------------------------
 * dwrite — Write sectors to disk via BIOS.
 *
 * ASM: DWRITE / WRTDRV  86DOS.asm:1150-1183
 *
 * Same retry logic as dread.
 * ----------------------------------------------------------------------- */
int dwrite(byte *buf, word count, word sector, byte *bp)
{
    byte drive = DPB_GET_BYTE(bp, DRVNUM);

    for (;;) {
        int err = BIOSWRITE(drive, buf, count, sector);
        if (!err) return 0;

        {
            int action = harderr_prompt(
                (const byte *)"\r\nDisk write error$",
                drive, &buf, &count, &sector, bp);
            if (action == 0) return 0;
            if (action == 1) return 1;
            /* retry */
        }
    }
}


/* -----------------------------------------------------------------------
 * harderr_prompt — Display error message and ask user what to do.
 *
 * ASM: HARDERR  86DOS.asm:1186-1214 *
 * The ASM adjusts BX (the buffer pointer) and DX (the sector) by DI
 * (the number of sectors that succeeded before the failure), using
 * SHFTDI7 (a 7-bit left-shift of DI) to convert the sector count into a
 * buffer offset.  In C we track remaining count/sector directly through
 * the pointers.
 *
 * Returns:
 *   0 — Ignore  (AL='i': pretend read/write succeeded)
 *   1 — Continue (AL='c': report error to caller)
 *   2 — Retry   (AL='r': retry the operation)
 * Abort ('a') calls fn_abort() directly and does not return.
 * ----------------------------------------------------------------------- */
static int harderr_prompt(const byte *msg, byte drive,
                          byte **buf_io, word *cx_io, word *dx_io,
                          byte *bp)
{
    (void)drive;
    (void)buf_io; (void)cx_io; (void)dx_io; (void)bp;

    con_outmes(msg);

    for (;;) {
        byte ch = fn_in();
        ch = (byte)(ch | 0x20);   /* to lower case */
        switch (ch) {
        case 'a':
            /* ABORT: fall through to ERROR (fn_abort exits) */
            /* NOTE: in the real kernel this unwinds to the user exit addr */
            con_outmes((const byte *)"\r\n$");
            /* We simulate by returning a fatal error code */
            return 1;
        case 'r':
            return 2;   /* Retry */
        case 'i':
            return 0;   /* Ignore */
        case 'c':
            return 1;   /* Continue */
        default:
            break;
        }
    }
}

/* Public wrapper matching dos.h prototype */
int harderr(const byte *msg, byte drive, byte *buf, word *cx, word *dx)
{
    return harderr_prompt(msg, drive, &buf, cx, dx, NULL);
}


/* -----------------------------------------------------------------------
 * dircomp — Compute parameters for directory sector I/O.
 *
 * ASM: DIRCOMP  86DOS.asm:963-972
 *
 * Inputs:
 *   al — directory block number (relative sector within directory)
 *   bp — DPB pointer
 * Outputs:
 *   *sector_out — absolute sector number  (DX in ASM: al + FIRDIR)
 *   *buf_out    — DIRBUF pointer           (BX = DIRBUF)
 *   *cnt_out    — sector count = 1         (CX = 1)
 * ----------------------------------------------------------------------- */
static void dircomp(byte al, byte *bp,
                    word *sector_out, byte **buf_out, word *cnt_out)
{
    word firdir = DPB_GET_WORD(bp, FIRDIR);
    *sector_out = (word)(firdir + al);
    *buf_out    = dos->DIRBUF;
    *cnt_out    = 1;
}


/* -----------------------------------------------------------------------
 * dirread — Read a directory sector into DIRBUF.
 *
 * ASM: DIRREAD  86DOS.asm:1071-1094
 *
 * Inputs:
 *   al — directory block number
 *   bp — DPB pointer
 * Side effects:
 *   dos->DIRBUFID set to (drvnum << 8 | al).
 * ----------------------------------------------------------------------- */
void dirread(byte al, byte *bp)
{
    byte  drvnum = DPB_GET_BYTE(bp, DRVNUM);
    word  sector;
    byte *buf;
    word  cnt;

    chkdirwrite(bp);            /* flush dirty dir buffer first            */

    dos->DIRBUFID = (word)((drvnum << 8) | al);  /* MOV [DIRBUFID],AX    */

    dircomp(al, bp, &sector, &buf, &cnt);
    dread(buf, cnt, sector, bp);
}


/* -----------------------------------------------------------------------
 * chkdirwrite — Write DIRBUF to disk only if dirty.
 *
 * ASM: CHKDIRWRITE  86DOS.asm:1129-1132
 * ----------------------------------------------------------------------- */
void chkdirwrite(byte *bp)
{
    if (dos->DIRTYDIR)
        dirwrite(0, bp);    /* al is recovered from DIRBUFID inside */
}


/* -----------------------------------------------------------------------
 * dirwrite — Write the current DIRBUF to disk.
 *
 * ASM: DIRWRITE  86DOS.asm:1133-1149
 *      (also CHKDIRWRITE falls through here)
 *
 * Note: the ASM uses the low byte of DIRBUFID as the directory block
 * number (AL).  The 'al' parameter here is ignored; we extract it from
 * DIRBUFID to match the ASM exactly.
 * ----------------------------------------------------------------------- */
void dirwrite(byte al, byte *bp)
{
    word  sector;
    byte *buf;
    word  cnt;

    (void)al;   /* NOTE: ASM reloads AL from DIRBUFID at line 1146 */
    dos->DIRTYDIR = 0;

    al = (byte)(dos->DIRBUFID & 0xFF);   /* MOV AL,[DIRBUFID]             */
    dircomp(al, bp, &sector, &buf, &cnt);
    dwrite(buf, cnt, sector, bp);
}


/* -----------------------------------------------------------------------
 * bufsec — Ensure a given file sector is in the sector buffer.
 *
 * ASM: BUFSEC  86DOS.asm:1508-1566
 *
 * Inputs:
 *   no_preread — if non-zero, skip reading existing data (write-only)
 *   bp         — DPB pointer
 *   (uses dos->CLUSNUM, dos->SECCLUSPOS, dos->BYTCNT1, dos->BYTSECPOS,
 *         dos->NEXTADD, dos->BUFFER, dos->BUFSECNO, dos->BUFDRVNO,
 *         dos->DIRTYBUF)
 * Outputs:
 *   *si_out — pointer into buffer at the correct byte offset
 *   *di_out — pointer to DMA transfer address
 *   *cx_out — number of bytes to transfer
 *   dos->NEXTADD advanced by BYTCNT1
 *   dos->TRANS set to 1
 * ----------------------------------------------------------------------- */
void bufsec(int no_preread, byte *bp,
            byte **si_out, byte **di_out, word *cx_out)
{
    byte  drvnum = DPB_GET_BYTE(bp, DRVNUM);
    word  sector = fat_figrec(dos->CLUSNUM,
                              dos->SECCLUSPOS, bp);
    byte *buf    = dos->BUFFER;

    if (!no_preread) {
        /* Check if we already have the right sector buffered (FINBUF) */
        if (sector == dos->BUFSECNO && drvnum == dos->BUFDRVNO)
            goto finbuf;
        /* Need to read — flush dirty buffer first (GETSEC, ASM ~1535) */
        if (dos->DIRTYBUF) {
            dwrite(buf, 1, dos->BUFSECNO,
                   dos->DRVTAB[(int)dos->BUFDRVNO]);
        }
        dread(buf, 1, sector, bp);
    }

    /* SETBUF (ASM lines 1551-1555) */
    dos->BUFSECNO = sector;
    dos->BUFDRVNO = drvnum;

finbuf:
    dos->TRANS = 1;
    /* DEVIATION (host port): bufsec/bufrd/bufwrt are dead-but-linked here
     * (live versions are in io.c).  Mirror io.c's DMABASE+offset pattern so
     * if these ever go live they don't trip the same DMA-truncation bug. */
    *di_out    = dos->DMABASE + dos->NEXTADD;
    *cx_out    = dos->BYTCNT1;
    dos->NEXTADD = (word)(dos->NEXTADD + dos->BYTCNT1);
    *si_out    = buf + dos->BYTSECPOS;
}


/* -----------------------------------------------------------------------
 * bufrd — Copy from sector buffer to DMA area.
 *
 * ASM: BUFRD  86DOS.asm:1567-1580
 * ----------------------------------------------------------------------- */
void bufrd(byte *bp)
{
    byte *si, *di;
    word  cx;

    bufsec(0, bp, &si, &di, &cx);    /* XOR AL,AL → no_preread=0 */
    memcpy(di, si, cx);
}


/* -----------------------------------------------------------------------
 * bufwrt — Copy from DMA area to sector buffer.
 *
 * ASM: BUFWRT  86DOS.asm:1581-1607
 * ----------------------------------------------------------------------- */
void bufwrt(byte *bp)
{
    byte *si, *di;
    word  cx;
    int   no_preread;

    /* Advance SECPOS, compare with VALSEC (ASM lines 1582-1589) */
    dos->SECPOS++;
    no_preread = (dos->SECPOS > dos->VALSEC) ? 1 : 0;

    bufsec(no_preread, bp, &si, &di, &cx);
    /* bufsec returns si=buf+bytsecpos, di=DMA; we write DMA→buf */
    memcpy(si, di, cx);              /* XCHG DI,SI then MOVW */
    dos->DIRTYBUF = 1;
}


/* -----------------------------------------------------------------------
 * nextsec — Advance cluster position to the next sector.
 *
 * ASM: NEXTSEC  86DOS.asm:1608-1706
 *
 * Returns 0 (CLC) if more sectors available, -1 (STC) if at EOF.
 * ----------------------------------------------------------------------- */
int nextsec(byte *bp, byte *si)
{
    byte al;

    if (!dos->TRANS) return 0;   /* no transfer — CLC, nothing to do */

    al = (byte)(dos->SECCLUSPOS + 1);
    if (al > DPB_GET_BYTE(bp, CLUSMSK)) {
        /* Need next cluster */
        word bx = dos->CLUSNUM;
        if (bx >= FAT_EOF_MIN) {
            return -1;   /* STC — no next cluster */
        }
        {
            word next = fat_unpack(si, bx, bp);
            dos->CLUSNUM = next;
            dos->LASTPOS++;
            al = 0;
        }
    }
    dos->SECCLUSPOS = al;
    return 0;   /* CLC */
}
