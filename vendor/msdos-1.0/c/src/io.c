/*
 * io.c — Sequential and random record I/O for DOS file system.
 *
 * Translated from 86DOS.asm.  ASM labels covered:
 *
 *   SEQRD       86DOS.asm:1256-1259  — system call 20: sequential read
 *   SEQWRT      86DOS.asm:1261-1264  — system call 21: sequential write
 *   FINSEQ      86DOS.asm:1264-1268  — finish sequential op, update NR
 *   RNDRD       86DOS.asm:1270-1273  — system call 33: random read
 *   RNDWRT      86DOS.asm:1275-1278  — system call 34: random write
 *   BLKRD       86DOS.asm:1280-1283  — system call 39: block read
 *   BLKWRT      86DOS.asm:1285-1288  — system call 40: block write
 *   FINBLK      86DOS.asm:1288-1299  — finish block op, update CX saved
 *   FINRND      86DOS.asm:1294-1299  — finish random op, update RR field
 *   SETNREX     86DOS.asm:1303-1317  — write back NR/EXTENT fields, return DSKERR
 *   GETRRPOS1   86DOS.asm:1319-1325  — get RR pos (CX=1)
 *   GETRRPOS    86DOS.asm:1321-1325  — get RR pos (CX from caller)
 *   GETREC      86DOS.asm:2146-2168  — compute record pos from NR/EXTENT
 *   SETUP       86DOS.asm:1333-1431  — set up I/O state for a transfer
 *   BREAKDOWN   86DOS.asm:1433-1463  — split CX bytes into partial+whole+partial
 *   LOAD        86DOS.asm:1707-1821  — disk read path
 *   STORE       86DOS.asm:1888-2008  — disk write path
 *   WRTEOF      86DOS.asm:2013-2052  — write EOF (truncate or extend)
 *   KILLFIL     86DOS.asm:2045-2052  — release all clusters of a file
 *   RELFILE     86DOS.asm:2038-2043  — release tail of cluster chain
 *   NEXTSEC     86DOS.asm:1608-1630  — advance to next sector/cluster
 *   BUFSEC      86DOS.asm:1508-1565  — ensure sector is in buffer
 *   BUFRD       86DOS.asm:1567-1579  — buffered sector read
 *   BUFWRT      86DOS.asm:1581-1606  — buffered sector write
 *   TRANBUF     86DOS.asm:1632-1657  — transfer console input buffer
 *   READDEV     86DOS.asm:1659-1699  — read from character device
 *   WRTDEV      86DOS.asm:1834-1867  — write to character device
 *   OPTIMIZE    86DOS.asm:2055-2123  — find maximal sequential sector run
 *   SETFCB      86DOS.asm:1775-1821  — update FCB cluster/position fields
 *   ADDREC      86DOS.asm:1814-1821  — compute RECPOS+count
 */

#include <string.h>
#include "../include/dos.h"

/* -----------------------------------------------------------------------
 * Forward declarations for all functions in this file
 * ----------------------------------------------------------------------- */
static int   io_setup(byte *fcb, dword recpos, word reccnt,
                      byte **bp_out, byte **si_out);
static void  io_breakdown(word cx, byte *bp);
static int   io_nextsec(byte *bp, byte *si);
static void  io_bufsec(byte no_preread, byte *bp);
static void  io_bufrd(byte *bp);
static void  io_bufwrt(byte *bp);
static void  io_optimize(byte *bp, byte *si,
                         word *cx_inout, word *bx_inout, byte *dl_inout,
                         word *ax_out, word *dx_out, byte **bx_ptr_out);
static void  io_setfcb(byte *bp, word *ax_out, word *dx_out, word *cx_out);
static byte  io_finrnd(byte *fcb, word cx);
static byte  io_setnrex(byte *fcb, word ax, word dx);
static void  io_wrteof(byte *fcb, byte *bp, byte *si);
static void  io_readdev(byte *fcb, byte *bp, byte *si);
static void  io_wrtdev(byte *fcb, byte *bp, byte *si);
void         io_load(byte *fcb, byte *bp, byte *si);
void         io_store(byte *fcb, byte *bp, byte *si);

/* -----------------------------------------------------------------------
 * getrec — Compute record position from FCB NR and EXTENT fields.
 *
 * ASM: GETREC  86DOS.asm:2146-2168
 *
 * Inputs:
 *   fcb — pointer to user FCB (DS:DX in ASM)
 * Outputs:
 *   *cx_out — 1 (record count for sequential ops)
 *   *recpos_out — 32-bit record position derived from EXTENT:NR
 *
 * ASM:
 *   DI = DX
 *   CX = 1
 *   AL = [DI+NR]
 *   DX = [DI+EXTENT]
 *   SHL AL          ; AL <<= 1 (bit 7 of NR → CF)
 *   SHR DX          ; DX >>= 1 (CF → bit 15 of DX)
 *   RCR AL          ; AL = (CF_from_DX_SHR << 7) | (AL >> 1 with old CF)
 *   AH = DL
 *   DL = DH
 *   DH = 0
 * The resulting DX:AX is the 32-bit record position.
 * ----------------------------------------------------------------------- */
word fn_getrec(byte *fcb, word *dx_out)
{
    byte al   = FCB_GET_BYTE(fcb, NR);
    word dx   = FCB_GET_WORD(fcb, EXTENT);

    /* SHL AL */
    byte cf_al = (al & 0x80) ? 1 : 0;
    al = (byte)(al << 1);

    /* SHR DX */
    byte cf_dx = (byte)(dx & 1);
    dx >>= 1;
    /* RCR AL: bring in cf_dx */
    al = (byte)((cf_dx << 7) | (al >> 1) | (cf_al ? 0 : 0));
    /* NOTE: RCR brings the carry (from SHR DX) into the top bit of AL */
    al = (byte)((cf_dx << 7) | (al & 0x7F));

    word ax = (word)al | ((word)(dx & 0xFF) << 8);  /* AH = DL */
    *dx_out = (word)((dx >> 8) & 0xFF);              /* DL = DH, DH = 0 */
    return ax;
}

/* -----------------------------------------------------------------------
 * fn_seqrd — Sequential record read (system call 20).
 *
 * ASM: SEQRD  86DOS.asm:1256-1259
 * ----------------------------------------------------------------------- */
byte fn_seqrd(byte *fcb)
{
    word dx_out = 0;
    word ax = fn_getrec(fcb, &dx_out);
    word cx = 1;
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx_out, ax), cx, &bp, &si) != 0)
        goto finseq;
    io_load(fcb, bp, si);

finseq:
    /* FINSEQ: if CX != 0 advance AX:DX by 1 */
    {
        word finax = dos->RECPOS & 0xFFFF;
        word findx = (word)(dos->RECPOS >> 16);
        if (dos->RECCNT != 0) {
            finax++;
            if (finax == 0) findx++;
        }
        /* SETNREX path */
        return io_setnrex(fcb, finax, findx);
    }
}

/* -----------------------------------------------------------------------
 * fn_seqwrt — Sequential record write (system call 21).
 *
 * ASM: SEQWRT  86DOS.asm:1261-1264
 * ----------------------------------------------------------------------- */
byte fn_seqwrt(byte *fcb)
{
    word dx_out = 0;
    word ax = fn_getrec(fcb, &dx_out);
    word cx = 1;
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx_out, ax), cx, &bp, &si) != 0)
        goto finseq;
    io_store(fcb, bp, si);

finseq:
    {
        word finax = dos->RECPOS & 0xFFFF;
        word findx = (word)(dos->RECPOS >> 16);
        if (dos->RECCNT != 0) { finax++; if (finax == 0) findx++; }
        return io_setnrex(fcb, finax, findx);
    }
}

/* -----------------------------------------------------------------------
 * fn_rndrd — Random record read (system call 33).
 *
 * ASM: RNDRD  86DOS.asm:1270-1273
 * ----------------------------------------------------------------------- */
byte fn_rndrd(byte *fcb)
{
    /* GETRRPOS1: CX=1, AX:DX = FCB.RR */
    word ax = FCB_GET_WORD(fcb, RR);
    word dx = (word)(FCB_GET_WORD(fcb, RR+2) & 0x00FF) |
              ((word)(FCB_GET_BYTE(fcb, RR+3)) << 8);
    word cx = 1;
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx, ax), cx, &bp, &si) != 0)
        goto finrnd;
    io_load(fcb, bp, si);

finrnd:
    return io_finrnd(fcb, cx);
}

/* -----------------------------------------------------------------------
 * fn_rndwrt — Random record write (system call 34).
 *
 * ASM: RNDWRT  86DOS.asm:1275-1278
 * ----------------------------------------------------------------------- */
byte fn_rndwrt(byte *fcb)
{
    word ax = FCB_GET_WORD(fcb, RR);
    word dx = (word)(FCB_GET_WORD(fcb, RR+2) & 0x00FF) |
              ((word)(FCB_GET_BYTE(fcb, RR+3)) << 8);
    word cx = 1;
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx, ax), cx, &bp, &si) != 0)
        goto finrnd;
    io_store(fcb, bp, si);

finrnd:
    return io_finrnd(fcb, cx);
}

/* -----------------------------------------------------------------------
 * fn_blkrd — Block read (system call 39).
 *
 * ASM: BLKRD  86DOS.asm:1280-1283
 * ----------------------------------------------------------------------- */
byte fn_blkrd(byte *fcb, word cx, word *cx_out)
{
    word ax = FCB_GET_WORD(fcb, RR);
    word dx = (word)(FCB_GET_WORD(fcb, RR+2) & 0x00FF) |
              ((word)(FCB_GET_BYTE(fcb, RR+3)) << 8);
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx, ax), cx, &bp, &si) != 0) {
        *cx_out = 0;
        goto finblk;
    }
    io_load(fcb, bp, si);

finblk:
    /* FINBLK: save CX back via saved register frame, then FINRND */
    *cx_out = dos->RECCNT;
    return io_finrnd(fcb, cx);
}

/* -----------------------------------------------------------------------
 * fn_blkwrt — Block write (system call 40).
 *
 * ASM: BLKWRT  86DOS.asm:1285-1288
 * ----------------------------------------------------------------------- */
byte fn_blkwrt(byte *fcb, word cx, word *cx_out)
{
    word ax = FCB_GET_WORD(fcb, RR);
    word dx = (word)(FCB_GET_WORD(fcb, RR+2) & 0x00FF) |
              ((word)(FCB_GET_BYTE(fcb, RR+3)) << 8);
    byte *bp = NULL, *si = NULL;

    if (io_setup(fcb, MAKE32(dx, ax), cx, &bp, &si) != 0) {
        *cx_out = 0;
        goto finblk;
    }
    io_store(fcb, bp, si);

finblk:
    *cx_out = dos->RECCNT;
    return io_finrnd(fcb, cx);
}

/* -----------------------------------------------------------------------
 * io_finrnd — Common finish path for random/block ops.
 *
 * ASM: FINRND  86DOS.asm:1294-1317 (merged with SETNREX)
 *
 * Updates FCB.RR with the next record position, then falls through to
 * SETNREX to update NR and EXTENT.
 * ----------------------------------------------------------------------- */
static byte io_finrnd(byte *fcb, word cx)
{
    word ax = (word)(dos->RECPOS & 0xFFFF);
    word dx = (word)(dos->RECPOS >> 16);

    /* FINRND: if CX != 0, ax:dx += 1 */
    if (cx != 0) {
        ax++;
        if (ax == 0) dx++;
    }

    return io_setnrex(fcb, ax, dx);
}

/* -----------------------------------------------------------------------
 * io_setnrex — Write NR and EXTENT fields back to FCB, return DSKERR.
 *
 * ASM: SETNREX  86DOS.asm:1303-1317
 *
 * Inputs:
 *   fcb   — pointer to FCB
 *   ax,dx — new record position (32-bit: DX:AX)
 * Outputs:
 *   Returns DSKERR.
 *   Updates FCB.RR[0..3], FCB.NR, FCB.EXTENT.
 *
 * ASM:
 *   MOV [DI+RR],AX
 *   MOV [DI+RR+2],DL
 *   if DH != 0: MOV [DI+RR+3],DH
 *   CX = AX
 *   AL &= 0x7F → [DI+NR] = AL
 *   CL &= 0x80  SHL CX  RCL DX
 *   AL = CH  AH = DL
 *   [DI+EXTENT] = AX
 *   AL = [DSKERR]
 * ----------------------------------------------------------------------- */
static byte io_setnrex(byte *fcb, word ax, word dx)
{
    FCB_SET_WORD(fcb, RR,   ax);
    FCB_SET_BYTE(fcb, RR+2, (byte)(dx & 0xFF));
    if (dx & 0xFF00)
        FCB_SET_BYTE(fcb, RR+3, (byte)(dx >> 8));

    /* NR = AX & 0x7F */
    FCB_SET_BYTE(fcb, NR, (byte)(ax & 0x7F));

    /* EXTENT computation:
     * CX = ax, CL &= 0x80, SHL CX (16-bit), RCL DX (bring bit 15 of CX in)
     * AL = CH, AH = DL (low byte of dx)
     */
    {
        word cx  = ax;
        byte cf;
        cx = (word)(cx & 0x80);   /* CL &= 0x80 */
        cf = (cx & 0x8000) ? 1 : 0;
        cx <<= 1;                  /* SHL CX */
        /* RCL DX: dx = (dx << 1) | cf */
        dx = (word)((dx << 1) | cf);
        word extent = (word)((cx >> 8) & 0xFF) |    /* AL = CH */
                      (word)((dx & 0xFF) << 8);      /* AH = DL */
        FCB_SET_WORD(fcb, EXTENT, extent);
    }

    return dos->DSKERR;
}

/* -----------------------------------------------------------------------
 * fn_setdma — Set DMA (disk transfer) address (system call 26).
 *
 * ASM: SETDMA  86DOS.asm (syscall 26)
 * ----------------------------------------------------------------------- */
byte fn_setdma(byte *seg, word dx)
{
    dos->DMABASE = seg;
    dos->DMAADD  = dx;
    dos->DMASEG  = 0;   /* legacy field, unused on host */
    return 0;
}

/* -----------------------------------------------------------------------
 * io_setup — Set up transfer state variables.
 *
 * ASM: SETUP  86DOS.asm:1333-1431
 *
 * Inputs:
 *   fcb     — pointer to user FCB
 *   recpos  — 32-bit record position in file
 *   reccnt  — number of records to transfer
 * Outputs:
 *   Returns 0 on success (sets dos->* transfer variables).
 *   Returns -1 if CX would be trimmed to 0 (NOROOM) or bad drive.
 *   *bp_out — DPB for the drive
 *   *si_out — FAT pointer (= DPB_FAT_PTR(bp))
 *   If return is -1, DSKERR is set.
 *
 * NOTE: In ASM, SETUP returns 1 level up with CX=0 on error.  In C we
 *       return -1 and the caller skips the I/O operation.
 * ----------------------------------------------------------------------- */
static int io_setup(byte *fcb, dword recpos, word reccnt,
                    byte **bp_out, byte **si_out)
{
    byte drive;
    byte *bp;
    word recsiz;
    dword bytpos;
    word bytpos_lo, bytpos_hi;
    word secpos;
    word bytsecpos;
    word clusnum;
    word cx;

    drive  = fcb[0];    /* 1-based drive number */
    recsiz = FCB_GET_WORD(fcb, RECSIZ);
    if (recsiz == 0) {
        recsiz = 128;
        FCB_SET_WORD(fcb, RECSIZ, 128);
    }

    bp = getbp((byte)(drive - 1));
    if (bp == NULL) {
        dos->DSKERR = (byte)0xFE;  /* NOFILERR: -2 */
        return -1;
    }
    *bp_out = bp;

    /* High byte of record position: if recsiz >= 64 ignore MSB */
    if (recsiz >= 64) {
        recpos &= 0x00FFFFFFul;   /* zero top byte */
    }

    dos->RECCNT  = reccnt;
    dos->RECPOS  = recpos;
    dos->FCB_PTR = fcb;
    dos->NEXTADD = dos->DMAADD;
    dos->DSKERR  = 0;
    dos->TRANS   = 0;

    /* BYTPOS = recpos * recsiz (32-bit multiply) */
    {
        word recpos_lo = (word)(recpos & 0xFFFF);
        word recpos_hi = (word)(recpos >> 16);
        dword bp_lo = (dword)recpos_lo * recsiz;
        dword bp_hi = (dword)recpos_hi * recsiz + (bp_lo >> 16);
        if (bp_hi >> 16) {
            /* Overflow: EOFERR */
            dos->DSKERR = 1;
            return -1;
        }
        bytpos    = (bp_hi << 16) | (bp_lo & 0xFFFF);
        bytpos_lo = (word)(bytpos & 0xFFFF);
        bytpos_hi = (word)(bytpos >> 16);
        dos->BYTPOS = bytpos;
    }

    {
        word secsiz = DPB_GET_WORD(bp, SECSIZ);
        secpos    = (word)(bytpos / secsiz);
        bytsecpos = (word)(bytpos % secsiz);
        dos->SECPOS    = secpos;
        dos->BYTSECPOS = bytsecpos;
    }

    {
        byte clusshft = DPB_GET_BYTE(bp, CLUSSHFT);
        byte clusmsk  = DPB_GET_BYTE(bp, CLUSMSK);
        dos->SECCLUSPOS = (byte)(secpos & clusmsk);
        clusnum = secpos >> clusshft;
        dos->CLUSNUM = clusnum;
    }

    /* CX = reccnt * recsiz (number of bytes to transfer) */
    {
        dword total = (dword)reccnt * recsiz;
        if (total > 0xFFFFu) {
            /* trim to segment boundary */
        }
        cx = (word)(total & 0xFFFF);
        /* Check if transfer fits in segment */
        {
            dword end = (dword)dos->DMAADD + total;
            if (end > 0xFFFFu) {
                /* Trim: figure how many records fit */
                word room  = (word)(0xFFFFu - dos->DMAADD + 1u);
                word fit   = room / recsiz;
                if (fit == 0) {
                    dos->DSKERR = 2;
                    return -1;
                }
                cx = fit * recsiz;
                dos->DSKERR = 2;  /* trimming took place */
            }
        }
    }

    *si_out = DPB_FAT_PTR(bp);
    /* Abuse cx: store byte count back via caller */
    dos->RECCNT = (word)(cx / (FCB_GET_WORD(fcb, RECSIZ) ? FCB_GET_WORD(fcb, RECSIZ) : 128));
    /* Actually pass cx through a side channel — RECCNT already holds record count;
     * we need byte count for I/O.  Store in BYTCNT1 temporarily.                */
    dos->BYTCNT1 = cx;   /* byte count for this transfer */
    return 0;
}

/* -----------------------------------------------------------------------
 * io_breakdown — Compute BYTCNT1, SECCNT, BYTCNT2 from transfer length.
 *
 * ASM: BREAKDOWN  86DOS.asm:1433-1463
 *
 * Inputs (from dos->* state):
 *   dos->BYTSECPOS — byte offset in the first sector
 *   cx             — total bytes to transfer
 *   bp             — DPB
 * Outputs (written into dos->*):
 *   dos->BYTCNT1   — bytes in first (partial) sector
 *   dos->SECCNT    — whole sectors to transfer
 *   dos->BYTCNT2   — bytes in last  (partial) sector
 * ----------------------------------------------------------------------- */
static void io_breakdown(word cx, byte *bp)
{
    word secsiz   = DPB_GET_WORD(bp, SECSIZ);
    word bytsecpos = dos->BYTSECPOS;
    word bx       = cx;   /* total bytes */
    word ax;

    if (bytsecpos != 0) {
        ax = secsiz - bytsecpos;   /* bytes left in first sector */
        if ((sword)((sword)bx - (sword)ax) < 0) {
            /* Total fits in first sector */
            ax += bx;
            bx = 0;
        } else {
            bx -= ax;
        }
    } else {
        ax = 0;
    }
    dos->BYTCNT1 = ax;

    {
        word whole   = bx / secsiz;
        word partial = bx % secsiz;
        dos->SECCNT  = whole;
        dos->BYTCNT2 = partial;
    }
}

/* -----------------------------------------------------------------------
 * io_nextsec — Advance SECCLUSPOS; follow FAT to next cluster if needed.
 *
 * ASM: NEXTSEC  86DOS.asm:1608-1706
 *
 * Outputs:
 *   Returns 0 (CF clear) if next sector/cluster is available.
 *   Returns -1 (CF set) if at end of chain.
 * ----------------------------------------------------------------------- */
static int io_nextsec(byte *bp, byte *si)
{
    if (!dos->TRANS)
        return 0;   /* CLRET */

    {
        byte al       = dos->SECCLUSPOS;
        byte clusmsk  = DPB_GET_BYTE(bp, CLUSMSK);

        al++;
        if (al <= clusmsk) {
            dos->SECCLUSPOS = al;
            return 0;
        }
        /* Need next cluster */
        {
            word bx = dos->CLUSNUM;
            if (bx >= 0xFF8u) {
                return -1;   /* NONEXT: CF set */
            }
            {
                word di = fat_unpack(si, bx, bp);
                dos->CLUSNUM = di;
                dos->LASTPOS++;
                dos->SECCLUSPOS = 0;
                return 0;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * io_bufsec — Ensure the required sector is in the sector buffer.
 *
 * ASM: BUFSEC  86DOS.asm:1508-1565
 *
 * Inputs:
 *   no_preread — 1 = skip preread (write path for new sectors)
 *   bp         — DPB
 * Side effects: updates dos->BUFSECNO, dos->BUFDRVNO, dos->BUFFER area,
 *               sets dos->TRANS = 1, updates dos->NEXTADD.
 * ----------------------------------------------------------------------- */
static void io_bufsec(byte no_preread, byte *bp)
{
    word dx;
    byte bl;

    dx = dos->CLUSNUM;
    bl = dos->SECCLUSPOS;
    /* FIGREC: compute physical sector number */
    dx = fat_figrec(dx, bl, bp);

    if (!no_preread) {
        /* Check if already in buffer */
        if (dx == dos->BUFSECNO && dos->BUFDRVNO == DPB_GET_BYTE(bp, DRVNUM))
            goto finbuf;
        /* Need to read it — flush dirty buffer first */
        if (dos->DIRTYBUF) {
            byte old_drv = dos->BUFDRVNO;
            /* Write old buffer back using its drive's DPB */
            /* (We use a simplified path: use bp for dwrite) */
            dos->DIRTYBUF = 0;
            dwrite(dos->BUFFER, 1, dos->BUFSECNO, bp);
        }
        /* Read the sector */
        dread(dos->BUFFER, 1, dx, bp);
    }

    dos->BUFSECNO = dx;
    dos->BUFDRVNO = DPB_GET_BYTE(bp, DRVNUM);

finbuf:
    dos->TRANS   = 1;
    /* NEXTADD and BYTCNT1 are consumed by caller */
}

/* -----------------------------------------------------------------------
 * io_bufrd — Read BYTCNT1 bytes from buffer sector into DMA area.
 *
 * ASM: BUFRD  86DOS.asm:1567-1579
 * ----------------------------------------------------------------------- */
static void io_bufrd(byte *bp)
{
    io_bufsec(0, bp);   /* AL=0: preread */
    {
        word  cx  = dos->BYTCNT1;
        byte *src = dos->BUFFER + dos->BYTSECPOS;
        byte *dst = (dos->DMABASE + dos->DMAADD) + dos->NEXTADD - dos->DMAADD;
        /* NOTE: in the original code ES:[DI] is the DMA segment + NEXTADD.
         * Here we use a flat pointer model.                               */
        memcpy(dst, src, cx);
        dos->NEXTADD += cx;
    }
}

/* -----------------------------------------------------------------------
 * io_bufwrt — Write BYTCNT1 bytes from DMA area into buffer sector.
 *
 * ASM: BUFWRT  86DOS.asm:1581-1606
 * ----------------------------------------------------------------------- */
static void io_bufwrt(byte *bp)
{
    byte no_preread;
    dos->SECPOS++;
    no_preread = (dos->SECPOS > dos->VALSEC) ? 1 : 0;
    io_bufsec(no_preread, bp);
    {
        word  cx  = dos->BYTCNT1;
        byte *dst = dos->BUFFER + dos->BYTSECPOS;
        byte *src = (dos->DMABASE + dos->DMAADD) + (dos->NEXTADD - dos->BYTCNT1);
        memcpy(dst, src, cx);
        dos->DIRTYBUF = 1;
    }
}

/* -----------------------------------------------------------------------
 * io_optimize — Find maximal run of contiguous sectors.
 *
 * ASM: OPTIMIZE  86DOS.asm:2055-2123
 *
 * Finds contiguous clusters in the FAT starting from dos->CLUSNUM and
 * returns the physical sector address and byte count for a single I/O.
 *
 * Inputs/outputs go through dos->* state; returned in out parameters.
 * ----------------------------------------------------------------------- */
static void io_optimize(byte *bp, byte *si,
                        word *cx_inout, word *bx_inout, byte *dl_inout,
                        word *ax_out, word *dx_out, byte **bx_ptr_out)
{
    word secsiz  = DPB_GET_WORD(bp, SECSIZ);
    byte clusmsk = DPB_GET_BYTE(bp, CLUSMSK);
    byte al = (byte)(clusmsk + 1);   /* sectors per cluster */
    byte ah = al;
    byte dl = *dl_inout;
    al = (byte)(al - dl);            /* sectors left in first cluster */
    word dx = *cx_inout;             /* total sectors to transfer */
    word bx = *bx_inout;            /* starting cluster */
    word cx = 0;                     /* sequential sectors found */

    word start_bx = bx;

    for (;;) {
        word di = fat_unpack(si, bx, bp);
        cx += al;
        if (cx >= dx) {
            /* BLKDON: we have more than needed */
            cx -= dx;
            ah -= (byte)cx;
            ah--;   /* adjust to sector position within cluster */
            dos->SECCLUSPOS = ah;
            cx = dx;
            break;
        }
        /* OPTCLUS: follow next cluster if contiguous */
        al = ah;
        bx++;
        if (di != bx) {
            bx--;
            break;
        }
    }

    /* FINCLUS */
    dos->CLUSNUM = bx;
    {
        word sectors_still = dx - cx;   /* remaining sectors after this run */
        dword nbytes = (dword)cx * secsiz;
        dos->NEXTADD += (word)nbytes;

        word new_clusters = bx - start_bx;
        dos->LASTPOS += new_clusters;

        /* Physical sector of starting cluster */
        word phys = fat_figrec(start_bx, *dl_inout, bp);

        *ax_out      = sectors_still;
        *dx_out      = phys;
        *bx_ptr_out  = (dos->DMABASE + dos->DMAADD) + (dos->NEXTADD - (word)nbytes);
        *cx_inout    = cx;
    }
}

/* -----------------------------------------------------------------------
 * io_setfcb — Update LSTCLUS/CLUSPOS in FCB and compute RECPOS result.
 *
 * ASM: SETFCB / SETCLUS / ADDREC  86DOS.asm:1775-1821
 *
 * Also handles partial-record padding (DSKERR=3) and record count.
 * ----------------------------------------------------------------------- */
static void io_setfcb(byte *bp, word *ax_out, word *dx_out, word *cx_out)
{
    byte *fcb   = dos->FCB_PTR;
    word recsiz = FCB_GET_WORD(fcb, RECSIZ);
    word nextadd = dos->NEXTADD;
    word bytes_xfr = nextadd - dos->DMAADD;

    /* Number of records transferred */
    word records = bytes_xfr / recsiz;
    word rem     = bytes_xfr % recsiz;

    if (records < dos->RECCNT) {
        dos->DSKERR = 1;   /* end of file: fewer records than requested */
        if (rem != 0) {
            /* Partial last record: zero-fill remainder */
            dos->DSKERR = 3;
            word fill = recsiz - rem;
            byte *fill_ptr = (dos->DMABASE + dos->DMAADD) + bytes_xfr;
            memset(fill_ptr, 0, fill);
            records++;   /* count partial record */
        }
    }

    *cx_out = records;

    /* SETCLUS */
    FCB_SET_WORD(fcb, LSTCLUS, dos->CLUSNUM);
    FCB_SET_WORD(fcb, CLUSPOS, dos->LASTPOS);

    /* ADDREC: AX:DX = RECPOS + (records - 1) */
    {
        word ax = (word)(dos->RECPOS & 0xFFFF);
        word dx = (word)(dos->RECPOS >> 16);
        word cnt = records;
        cnt--;
        ax += cnt;
        if (ax < cnt) dx++;
        cnt++;
        *ax_out = ax;
        *dx_out = dx;
    }
}

/* -----------------------------------------------------------------------
 * io_load — Perform disk read.
 *
 * ASM: LOAD  86DOS.asm:1707-1821
 * ----------------------------------------------------------------------- */
void io_load(byte *fcb, byte *bp, byte *si)
{
    word cx;
    word fildirblk = FCB_GET_WORD(fcb, FILDIRBLK);

    /* Check for device I/O */
    if ((fildirblk >> 8) == 0xFF) {
        io_readdev(fcb, bp, si);
        return;
    }

    /* Check file size vs BYTPOS */
    {
        word filsiz_lo = FCB_GET_WORD(fcb, FILSIZ);
        word filsiz_hi = FCB_GET_WORD(fcb, FILSIZ+2);
        word bytpos_lo = (word)(dos->BYTPOS & 0xFFFF);
        word bytpos_hi = (word)(dos->BYTPOS >> 16);
        word avail_lo, avail_hi;

        /* avail = filsiz - bytpos */
        avail_lo = filsiz_lo - bytpos_lo;
        avail_hi = filsiz_hi - bytpos_hi - (filsiz_lo < bytpos_lo ? 1 : 0);
        if ((sword)avail_hi < 0) {
            /* RDERR */
            dos->RECCNT = 0;
            dos->DSKERR = (byte)-2;
            return;
        }
        if (avail_hi == 0 && avail_lo == 0) {
            dos->RECCNT = 0;
            dos->DSKERR = (byte)-2;
            return;
        }
        /* Clamp byte count to available */
        if (avail_hi == 0 && avail_lo < dos->BYTCNT1) {
            dos->BYTCNT1 = avail_lo;
        }
    }

    cx = dos->BYTCNT1;   /* total bytes for this transfer */
    io_breakdown(cx, bp);

    /* Walk cluster chain to CLUSNUM */
    {
        word clusnum = dos->CLUSNUM;
        word bx_out, dx_out;
        word new_cx = clusnum;
        fat_fndclus(fcb, si, new_cx, bp, &bx_out, &dx_out);
        if (new_cx > 0 && bx_out == 0) {
            /* RDERR */
            dos->RECCNT = 0;
            dos->DSKERR = (byte)-2;
            return;
        }
        dos->LASTPOS = dx_out;
        dos->CLUSNUM = bx_out;
    }

    /* BUFRD for first partial sector */
    if (dos->BYTCNT1 != 0) {
        io_bufrd(bp);
    }

    /* RDMID: whole sectors */
    if (dos->SECCNT != 0) {
        if (io_nextsec(bp, si) != 0) goto setfcb;
        dos->TRANS = 1;
        {
            byte dl = dos->SECCLUSPOS;
            word cx_sec = dos->SECCNT;
            word bx_cl  = dos->CLUSNUM;
            while (cx_sec > 0) {
                word ax_rem, dx_phys;
                byte *xfr_buf;
                io_optimize(bp, si, &cx_sec, &bx_cl, &dl, &ax_rem, &dx_phys, &xfr_buf);
                dread(xfr_buf, cx_sec - ax_rem, dx_phys, bp);
                cx_sec = ax_rem;
                if (cx_sec == 0) goto rdlast;
                if (bx_cl >= 0xFF8u) goto setfcb;
                dl = 0;
                dos->LASTPOS++;
            }
        }
    }

rdlast:
    /* RDLAST: partial last sector */
    if (dos->BYTCNT2 != 0) {
        dos->BYTCNT1   = dos->BYTCNT2;
        dos->BYTSECPOS = 0;
        if (io_nextsec(bp, si) != 0) goto setfcb;
        io_bufrd(bp);
    }

setfcb:
    {
        word ax, dx, cx;
        io_setfcb(bp, &ax, &dx, &cx);
        dos->RECPOS  = MAKE32(dx, ax);
        dos->RECCNT  = cx;
    }
}

/* -----------------------------------------------------------------------
 * io_store — Perform disk write.
 *
 * ASM: STORE  86DOS.asm:1888-2008
 * ----------------------------------------------------------------------- */
void io_store(byte *fcb, byte *bp, byte *si)
{
    word fildirblk;

    FCB_SET_BYTE(fcb, DIRTYFIL, 1);
    FCB_SET_WORD(fcb, FDATE, dos->DATE);

    /* Setup was already done; reload state */
    fildirblk = FCB_GET_WORD(fcb, FILDIRBLK);
    if ((fildirblk >> 8) == 0xFF) {
        io_wrtdev(fcb, bp, si);
        return;
    }

    {
        word cx = dos->BYTCNT1;
        io_breakdown(cx, bp);
    }

    /* If no bytes, go to WRTEOF */
    if (dos->BYTPOS == 0 && (dos->BYTPOS >> 16) == 0 && dos->BYTCNT1 == 0) {
        io_wrteof(fcb, bp, si);
        return;
    }

    {
        /* Compute last sector to be accessed */
        word secsiz   = DPB_GET_WORD(bp, SECSIZ);
        word clusshft = DPB_GET_BYTE(bp, CLUSSHFT);
        dword last_byte = dos->BYTPOS + dos->BYTCNT1 - 1;
        word  last_sec  = (word)((last_byte) / secsiz);
        word  last_clus = last_sec >> clusshft;

        /* Compute VALSEC: sectors already written */
        {
            word filsiz_lo = FCB_GET_WORD(fcb, FILSIZ);
            word valsec    = filsiz_lo / secsiz;
            if (filsiz_lo % secsiz) valsec++;
            dos->VALSEC = valsec;
        }

        /* Walk cluster chain */
        {
            word clusnum = dos->CLUSNUM;
            word bx_cl, dx_pos;
            word cx_rem = last_clus;
            fat_fndclus(fcb, si, cx_rem, bp, &bx_cl, &dx_pos);
            dos->CLUSNUM = bx_cl;
            dos->LASTPOS = dx_pos;

            if (cx_rem != 0) {
                /* Need to allocate */
                word bx_new, cx_new;
                if (fat_allocate(si, bx_cl, cx_rem, dx_pos, bp,
                                 fcb, &bx_new, &cx_new) != 0) {
                    /* WRTERR */
                    dos->DSKERR = 1;
                    goto lvdsk;
                }
                dos->CLUSNUM = bx_new;
                dos->LASTPOS = cx_new;
            }
        }
    }

    /* Write first partial sector */
    if (dos->BYTCNT1 != 0) {
        io_bufwrt(bp);
    }

    /* Write whole sectors */
    if (dos->SECCNT != 0) {
        dos->SECPOS += dos->SECCNT;
        io_nextsec(bp, si);
        dos->TRANS = 1;
        {
            byte dl = dos->SECCLUSPOS;
            word cx_sec = dos->SECCNT;
            word bx_cl  = dos->CLUSNUM;
            while (cx_sec > 0) {
                word ax_rem, dx_phys;
                byte *xfr_buf;
                io_optimize(bp, si, &cx_sec, &bx_cl, &dl, &ax_rem, &dx_phys, &xfr_buf);
                dwrite(xfr_buf, cx_sec - ax_rem, dx_phys, bp);
                cx_sec = ax_rem;
                if (cx_sec == 0) break;
                dl = 0;
                dos->LASTPOS++;
            }
        }
    }

    /* Write last partial sector */
    if (dos->BYTCNT2 != 0) {
        dos->BYTCNT1   = dos->BYTCNT2;
        dos->BYTSECPOS = 0;
        io_nextsec(bp, si);
        io_bufwrt(bp);
    }

    /* Update FILSIZ if we extended the file */
    {
        word new_lo = (word)(dos->BYTPOS & 0xFFFF) + dos->NEXTADD - dos->DMAADD;
        word new_hi = (word)(dos->BYTPOS >> 16);
        if (new_lo < (dos->NEXTADD - dos->DMAADD)) new_hi++;

        word filsiz_lo = FCB_GET_WORD(fcb, FILSIZ);
        word filsiz_hi = FCB_GET_WORD(fcb, FILSIZ+2);
        if (new_hi > filsiz_hi || (new_hi == filsiz_hi && new_lo > filsiz_lo)) {
            FCB_SET_WORD(fcb, FILSIZ,   new_lo);
            FCB_SET_WORD(fcb, FILSIZ+2, new_hi);
        }
    }

    {
        word ax, dx, cx;
        io_setfcb(bp, &ax, &dx, &cx);
        dos->RECPOS = MAKE32(dx, ax);
        dos->RECCNT = cx;
    }
    return;

lvdsk:
    dos->RECPOS = MAKE32(0, 0);
    dos->RECCNT = 0;
}

/* -----------------------------------------------------------------------
 * io_wrteof — Write EOF / truncate or extend file.
 *
 * ASM: WRTEOF  86DOS.asm:2013-2052
 * ----------------------------------------------------------------------- */
static void io_wrteof(byte *fcb, byte *bp, byte *si)
{
    word bytpos_lo = (word)(dos->BYTPOS & 0xFFFF);
    word bytpos_hi = (word)(dos->BYTPOS >> 16);

    if (bytpos_lo == 0 && bytpos_hi == 0) {
        /* KILLFIL: release all clusters */
        word old_clus;
        old_clus = FCB_GET_WORD(fcb, FIRCLUS);
        FCB_SET_WORD(fcb, FIRCLUS, 0);
        if (old_clus != 0) {
            fat_release(si, old_clus, bp);
            DPB_SET_BYTE(bp, DIRTYFAT, 1);
        }
        goto update;
    }

    /* Find last cluster needed */
    {
        word secsiz   = DPB_GET_WORD(bp, SECSIZ);
        word clusshft = DPB_GET_BYTE(bp, CLUSSHFT);
        dword last_byte = ((dword)bytpos_hi << 16 | bytpos_lo) - 1;
        word last_sec   = (word)(last_byte / secsiz);
        word last_clus  = last_sec >> clusshft;
        word bx_cl, dx_pos, cx_rem;

        cx_rem = last_clus;
        fat_fndclus(fcb, si, cx_rem, bp, &bx_cl, &dx_pos);
        dos->CLUSNUM = bx_cl;
        dos->LASTPOS = dx_pos;

        if (cx_rem != 0) {
            /* Need to allocate more clusters */
            word bx_new, cx_new;
            if (fat_allocate(si, bx_cl, cx_rem, dx_pos, bp,
                             fcb, &bx_new, &cx_new) != 0) {
                dos->DSKERR = 1;
                goto update;
            }
        } else {
            /* Release tail of chain */
            fat_relblks(si, bx_cl, 0x0FFF, bp);
            DPB_SET_BYTE(bp, DIRTYFAT, 1);
        }
    }

update:
    FCB_SET_WORD(fcb, FILSIZ,   bytpos_lo);
    FCB_SET_WORD(fcb, FILSIZ+2, bytpos_hi);
    dos->RECCNT = 0;
}

/* -----------------------------------------------------------------------
 * io_readdev — Read from a character device.
 *
 * ASM: READDEV  86DOS.asm:1659-1699
 *
 * BL encodes the device: 0=CON, 1=AUX, 2+ = unused.
 * ----------------------------------------------------------------------- */
static void io_readdev(byte *fcb, byte *bp, byte *si)
{
    word  fildirblk = FCB_GET_WORD(fcb, FILDIRBLK);
    byte  bl        = (byte)(fildirblk & 0xFF);
    word  cx        = dos->BYTCNT1;
    byte *di        = dos->DMABASE + dos->DMAADD;

    if (bl == 0) {
        /* CON: read from console buffer */
        int i;
        for (i = 0; i < (int)cx; i++) {
            byte ch = BIOSIN();
            di[i] = ch;
            if (ch == 0x1A) {
                /* Ctrl-Z: mark no more data */
                FCB_SET_BYTE(fcb, FILDIRBLK, FCB_GET_BYTE(fcb, FILDIRBLK) | 0x80);
                break;
            }
            if (ch == '\r') { di[i+1] = '\n'; i++; }
            if (ch == '\n') break;
        }
        dos->NEXTADD = (word)(uintptr_t)(di + i);
    } else {
        bl--;
        if (bl == 0) {
            /* AUX */
            int i;
            for (i = 0; i < (int)cx; i++) {
                byte ch = BIOSAUXIN();
                di[i] = ch;
                if (ch == 0x1A) break;
            }
            dos->NEXTADD = (word)(uintptr_t)(di + i);
        }
    }
    {
        word ax, dx, cx_out;
        io_setfcb(bp, &ax, &dx, &cx_out);
        dos->RECPOS = MAKE32(dx, ax);
        dos->RECCNT = dos->RECCNT;  /* unchanged */
    }
}

/* -----------------------------------------------------------------------
 * io_wrtdev — Write to a character device.
 *
 * ASM: WRTDEV  86DOS.asm:1834-1867
 *
 * BL & 0x7F: 0=CON, 1=AUX, 2=LST
 * ----------------------------------------------------------------------- */
static void io_wrtdev(byte *fcb, byte *bp, byte *si)
{
    word fildirblk = FCB_GET_WORD(fcb, FILDIRBLK);
    byte bl = (byte)(fildirblk & 0x7F);
    word cx = dos->BYTCNT1;
    byte *src = dos->DMABASE + dos->DMAADD;
    word i;

    for (i = 0; i < cx; i++) {
        byte ch = src[i];
        if (ch == 0x1A) break;   /* Ctrl-Z: end */
        if (bl == 0)        BIOSOUT(ch);    /* CON */
        else if (bl == 1)   BIOSAUXOUT(ch); /* AUX */
        else                BIOSPRINT(ch);  /* LST */
    }

    dos->RECCNT = dos->RECCNT;
    {
        word ax = (word)(dos->RECPOS & 0xFFFF);
        word dx = (word)(dos->RECPOS >> 16);
        dos->RECPOS = MAKE32(dx, ax + dos->RECCNT);
    }
}
