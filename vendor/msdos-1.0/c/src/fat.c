/*
 * fat.c — FAT12 File Allocation Table routines.
 *
 * Translated from 86DOS.asm.  The following ASM labels are covered here:
 *
 *   UNPACK      86DOS.asm:369-401   — read one 12-bit FAT entry
 *   PACK        86DOS.asm:402-433   — write one 12-bit FAT entry
 *   FIGFAT      86DOS.asm:952-962   — prepare registers for FAT I/O
 *   FATWRT      86DOS.asm:914-951   — write FAT back to disk
 *   CHKFATWRT   86DOS.asm:908-913   — conditional FATWRT
 *   FATREAD     86DOS.asm:766-907   — read FAT from disk (if needed)
 *   GETEOF      86DOS.asm:2285-2301 — walk chain to last cluster
 *   RELEASE     86DOS.asm:2260-2271 — free a cluster chain
 *   RELBLKS     86DOS.asm:2272-2284 — partial chain free
 *   ALLOCATE    86DOS.asm:2169-2284 — allocate clusters for a file
 *   FNDCLUS     86DOS.asm:1466-1507 — walk cluster chain N steps
 *   FIGREC      86DOS.asm:2126-2145 — cluster+sector → physical sector
 *   WRTFATS     86DOS.asm:2490-2517 — write all dirty FATs
 */

#include <string.h>
#include "../include/dos.h"

/* -----------------------------------------------------------------------
 * fat_unpack — Read one 12-bit FAT entry.
 *
 * ASM: UNPACK  86DOS.asm:369-401
 *
 * Inputs:
 *   si  — pointer to the in-memory FAT (= &dpb->fat[0])
 *   bx  — cluster number to look up
 *   bp  — pointer to DPB (used for MAXCLUS bounds check)
 * Outputs:
 *   Returns the 12-bit FAT entry for cluster bx.
 *   Returns 0xFFFF on fatal error (cluster > MAXCLUS).
 *
 * The 12-bit packing (86DOS.asm lines 89-98):
 *   byte_offset = bx + (bx >> 1)   i.e. floor(bx * 1.5)
 *   word        = *(uint16_t*)(si + byte_offset)
 *   if bx even: entry = word & 0x0FFF
 *   if bx odd:  entry = word >> 4
 *
 * The carry flag after SHR BX (line 384) marks odd/even:
 *   NC (bx was even) → HAVCLUS  (mask only)
 *   C  (bx was odd)  → shift right 4 bits, then mask
 * ----------------------------------------------------------------------- */
word fat_unpack(byte *si, word bx, byte *bp)
{
    word maxclus = DPB_GET_WORD(bp, MAXCLUS);

    /* Bounds check — HURTFAT in ASM (lines 396-399) */
    if (bx > maxclus) {
        /* ASM prints "Bad FAT" and jumps to ERROR; here we return sentinel */
        con_outmes((const byte *)"\r\nBad FAT\r\n$");
        return 0xFFFF; /* caller must treat as fatal */
    }

    {
        /* Compute byte offset: bx + (bx/2) = floor(bx * 1.5)             */
        /* ASM: LEA DI,[SI+BX]  then  SHR BX  (BX becomes bx>>1)         */
        /* DI = SI + original_bx  ← index for the word fetch              */
        word  orig_bx = bx;
        int   odd     = bx & 1;          /* carry from SHR BX             */
        word  idx     = (word)(orig_bx + (bx >> 1)); /* SI+BX after SHR  */
        word  entry   = *(word *)(si + idx);

        if (odd) {
            /* ASM lines 387-391: SHR DI x4, STC, RCL BX (restores bx)  */
            entry >>= 4;
        }
        entry &= 0x0FFF;
        return entry;
    }
}


/* -----------------------------------------------------------------------
 * fat_pack — Write one 12-bit FAT entry.
 *
 * ASM: PACK  86DOS.asm:402-433
 *
 * Inputs:
 *   si  — pointer to the in-memory FAT
 *   bx  — cluster number
 *   dx  — 12-bit value to store
 * Outputs:
 *   FAT byte(s) updated in memory.
 * ----------------------------------------------------------------------- */
void fat_pack(byte *si, word bx, word dx)
{
    word  di   = bx;
    int   odd  = bx & 1;            /* carry from SHR DI  (line 418)      */

    /* byte offset = bx + (bx/2) — same formula as UNPACK */
    word  off  = (word)(di + (bx >> 1));
    word *ptr  = (word *)(si + off);
    word  prev = *ptr;

    if (odd) {
        /* ASM lines 421-425: SHL DX x4, AND DI,0FH */
        dx   <<= 4;
        prev  &= 0x000F;
    } else {
        /* ASM line 428: AND DI,0F000H */
        prev  &= 0xF000;
    }
    *ptr = (word)(prev | (dx & (odd ? 0xFFF0 : 0x0FFF)));
}


/* -----------------------------------------------------------------------
 * fat_figfat — Compute parameters for FAT read or write.
 *
 * ASM: FIGFAT  86DOS.asm:952-962
 *
 * Inputs:
 *   bp — pointer to DPB
 * Outputs (via pointers):
 *   *al_out — FATCNT (number of FAT copies)
 *   *bx_out — pointer to in-memory FAT (&dpb->fat[0])
 *   *cx_out — FATSIZ (sectors per FAT copy)
 *   *dx_out — FIRFAT (starting sector of FAT area)
 * ----------------------------------------------------------------------- */
void fat_figfat(byte *bp,
                byte  *al_out,
                byte **bx_out,
                word  *cx_out,
                word  *dx_out)
{
    *al_out = DPB_GET_BYTE(bp, FATCNT);
    *bx_out = DPB_FAT_PTR(bp);                      /* LEA BX,[BP+FAT]   */
    *cx_out = (word)DPB_GET_BYTE(bp, FATSIZ);
    *dx_out = DPB_GET_WORD(bp, FIRFAT);
}


/* -----------------------------------------------------------------------
 * fat_write — Write the in-memory FAT back to disk (all copies).
 *
 * ASM: FATWRT  86DOS.asm:914-951
 *
 * Inputs:
 *   bp — pointer to DPB
 * Outputs:
 *   Returns 0 (AL=0 in ASM).
 *   DIRTYFAT cleared.
 * ----------------------------------------------------------------------- */
int fat_write(byte *bp)
{
    byte  fatcnt;
    byte *fat_ptr;
    word  fatsiz;
    word  sector;

    DPB_SET_BYTE(bp, DIRTYFAT, DIRTYFAT_CLEAN);     /* MOV B,[BP+DIRTYFAT],0 */

    fat_figfat(bp, &fatcnt, &fat_ptr, &fatsiz, &sector);

    /* Write each FAT copy  (EACHFAT loop, ASM lines 929-941) */
    while (fatcnt--) {
        dwrite(fat_ptr, fatsiz, sector, bp);
        sector = (word)(sector + fatsiz);
    }
    return 0;
}


/* -----------------------------------------------------------------------
 * fat_check_write — Write FAT only if it is dirty.
 *
 * ASM: CHKFATWRT  86DOS.asm:908-913
 * ----------------------------------------------------------------------- */
int fat_check_write(byte *bp)
{
    if (DPB_GET_BYTE(bp, DIRTYFAT) == DIRTYFAT_DIRTY)
        return fat_write(bp);
    return 0;
}


/* -----------------------------------------------------------------------
 * fat_read — Read FAT from disk into memory (if disk may have changed).
 *
 * ASM: FATREAD  86DOS.asm:766-907
 *      (entered also from STARTSRCH via fall-through)
 *
 * Inputs:
 *   bp — pointer to DPB
 * Side effects:
 *   In-memory FAT updated.
 *   Buffer flagged invalid if disk changed.
 *   DIRTYFAT, FIRREC, MAXCLUS updated if SMALLDIR.
 * ----------------------------------------------------------------------- */
void fat_read(byte *bp)
{
    byte  drive = DPB_GET_BYTE(bp, DRVNUM);
    int   dskchg_result;
    byte  dirtyfat;

    /* Check if disk changed (ASM lines 779-783) */
    dskchg_result = BIOSDSKCHG(drive);
    dirtyfat      = DPB_GET_BYTE(bp, DIRTYFAT);

    /*
     * "new disk" if BIOS says changed (result < 0) OR DIRTYFAT is -1
     * (sign bit set).  ASM: OR AH,[BP+DIRTYFAT] / JS NEWDSK
     */
    if (dskchg_result < 0 || (sbyte)dirtyfat < 0) {
        goto newdsk;
    }

    /* AH=1 means "disk not changed" (ASM line 785) */
    if (dskchg_result == 1) return;

    /*
     * AH=0: unsure — check if buffer has dirty sector on this drive.
     * If so, disk has not changed.  ASM lines 786-788.
     */
    if ((byte)drive == dos->BUFDRVNO && dos->DIRTYBUF)
        return;

newdsk:
    /* Invalidate buffers if they belong to this drive (ASM lines 790-795) */
    if (drive == dos->BUFDRVNO) {
        dos->BUFSECNO  = 0;
        dos->BUFDRVNO  = 0xFF;
    }
    dos->DIRBUFID = 0xFFFF;

    /* Read each FAT copy until a good one is found (NEXTFAT, ASM ~796-840) */
    {
        byte  fatcnt;
        byte *fat_ptr;
        word  fatsiz;
        word  sector;
        byte  copies;

        fat_figfat(bp, &fatcnt, &fat_ptr, &fatsiz, &sector);
        copies = fatcnt;

        while (copies > 0) {
            int err = dread(fat_ptr, fatsiz, sector, bp);
            if (err == 0) {
                /* Good FAT read.  Update FIRREC/MAXCLUS for SMALLDIR. */
#if SMALLDIR
                {
                    byte dirsiz = DPB_GET_BYTE(bp, DIRSIZ);
                    word firrec, maxclus;
                    if ((sbyte)dirsiz == -1) {
                        /* 16-byte entries — use FIRREC1/MAXCLUS1 */
                        firrec  = DPB_GET_WORD(bp, FIRREC1);
                        maxclus = DPB_GET_WORD(bp, MAXCLUS1);
                    } else {
                        /* 32-byte entries — use FIRREC2/MAXCLUS2 */
                        firrec  = DPB_GET_WORD(bp, FIRREC2);
                        maxclus = DPB_GET_WORD(bp, MAXCLUS2);
                    }
                    DPB_SET_WORD(bp, FIRREC,  firrec);
                    DPB_SET_WORD(bp, MAXCLUS, maxclus);
                }
#endif
                /* If not all copies were good, rewrite them (ASM ~824-830) */
                if (copies < fatcnt) {
                    fat_write(bp);  /* rewrite bad copies */
                }
                return;
            }
            /* This copy was bad, try the next (BADFAT, ASM lines 832-840) */
            sector = (word)(sector + fatsiz);
            copies--;
        }

        /* All FAT copies bad — BADFATMES (ASM line 838) */
        con_outmes((const byte *)"\r\nAll FATs on disk are bad\r\n$");
        /* NOTE: ASM jumps back to FATREAD (retry loop).
         * In C we simply return; the caller will encounter errors. */
    }
}


/* -----------------------------------------------------------------------
 * fat_geteof — Walk a cluster chain to find the last cluster.
 *
 * ASM: GETEOF  86DOS.asm:2285-2301
 *
 * Inputs:
 *   si  — FAT pointer
 *   bx  — any cluster in the file
 *   bp  — DPB pointer
 * Outputs:
 *   Returns the last cluster in the chain (entry >= FAT_EOF_MIN).
 * ----------------------------------------------------------------------- */
word fat_geteof(byte *si, word bx, byte *bp)
{
    for (;;) {
        word entry = fat_unpack(si, bx, bp);
        if (entry >= FAT_EOF_MIN)
            return bx;
        bx = entry;
    }
}


/* -----------------------------------------------------------------------
 * fat_relblks — Free clusters from bx onward; optionally put EOF at bx.
 *
 * ASM: RELBLKS  86DOS.asm:2272-2284
 *      (RELEASE enters here with dx=0; RELBLKS entered with dx=0x0FFF)
 *
 * If dx == 0x0FFF: put EOF marker in cluster bx, then free rest of chain.
 * If dx == 0:      free entire chain starting at bx.
 *
 * Returns 0 always; side-effect: FAT entries updated.
 * ----------------------------------------------------------------------- */
int fat_relblks(byte *si, word bx, word dx, byte *bp)
{
    word entry = fat_unpack(si, bx, bp);
    if (entry == FAT_FREE) return 0;

    {
        word ax = entry;
        fat_pack(si, bx, dx);           /* write dx (0 or 0x0FFF) into bx */
        if (ax >= FAT_EOF_MIN) return 0; /* was already EOF — done         */
        bx = ax;
    }

    /* Continue freeing the rest of the chain (recurse = RELEASE label) */
    return fat_relblks(si, bx, FAT_FREE, bp);
}


/* -----------------------------------------------------------------------
 * fat_release — Free the entire cluster chain starting at bx.
 *
 * ASM: RELEASE  86DOS.asm:2260-2271
 * ----------------------------------------------------------------------- */
int fat_release(byte *si, word bx, byte *bp)
{
    /* XOR DX,DX then fall into RELBLKS */
    return fat_relblks(si, bx, FAT_FREE, bp);
}


/* -----------------------------------------------------------------------
 * fat_fndclus — Walk cluster chain CX steps, using cached last-cluster.
 *
 * ASM: FNDCLUS  86DOS.asm:1466-1507
 *
 * Inputs:
 *   fcb  — pointer to FCB (byte array); reads LSTCLUS, CLUSPOS, FIRCLUS
 *   si   — FAT pointer
 *   cx   — number of clusters to skip from start of file
 *   bp   — DPB pointer
 * Outputs:
 *   *bx_out — last cluster reached
 *   *dx_out — chain position of that cluster
 *   Returns remaining count (0 if destination reached, >0 if hit EOF).
 * ----------------------------------------------------------------------- */
word fat_fndclus(byte *fcb, byte *si, word cx, byte *bp,
                 word *bx_out, word *dx_out)
{
    word bx = FCB_GET_WORD(fcb, LSTCLUS);
    word dx = FCB_GET_WORD(fcb, CLUSPOS);

    if (bx == 0) {
        /* NOCLUS (ASM lines 1502-1505): no clusters yet */
        cx++;
        dx = (word)(dx - 1);  /* dx = 0xFFFF effectively */
        *bx_out = bx;
        *dx_out = dx;
        return cx;
    }

    /* Can we reuse the cached last-cluster? (ASM lines 1486-1491) */
    if (cx >= dx) {
        cx -= dx;               /* skip already-traversed clusters */
    } else {
        /* cx < dx: must restart from FIRCLUS */
        dx = 0;
        bx = FCB_GET_WORD(fcb, FIRCLUS);
    }

    /* SKPCLP: walk forward CX clusters (ASM lines 1494-1501) */
    while (cx > 0) {
        word entry = fat_unpack(si, bx, bp);
        if (entry >= FAT_EOF_MIN) break;   /* hit EOF */
        bx = entry;
        dx++;
        cx--;
    }

    *bx_out = bx;
    *dx_out = dx;
    return cx;          /* 0 = found destination; >0 = short (EOF) */
}


/* -----------------------------------------------------------------------
 * fat_figrec — Convert cluster number + intra-cluster sector to physical
 *              sector number.
 *
 * ASM: FIGREC  86DOS.asm:2126-2145
 *
 * Inputs:
 *   dx  — physical cluster number
 *   bl  — sector position within cluster
 *   bp  — DPB pointer
 * Outputs:
 *   Returns physical (absolute) sector number.
 * ----------------------------------------------------------------------- */
word fat_figrec(word dx, byte bl, byte *bp)
{
    byte  shft   = DPB_GET_BYTE(bp, CLUSSHFT);
    word  firrec = DPB_GET_WORD(bp, FIRREC);

    /*
     * ASM:  DEC DX / DEC DX / SHL DX,CL / OR DL,BL / ADD DX,FIRREC
     * Cluster numbering starts at 2, so subtract 2 before shifting.
     */
    dx = (word)((dx - 2) << shft);
    dx = (word)(dx | bl);
    dx = (word)(dx + firrec);
    return dx;
}


/* -----------------------------------------------------------------------
 * fat_write_all — Write back all dirty FATs for all drives.
 *
 * ASM: WRTFATS  86DOS.asm:2490-2517
 *
 * Also flushes the dirty sector buffer if one exists.
 * Called from ABORT (line 1232) and DSKRESET (line 2490).
 * ----------------------------------------------------------------------- */
void fat_write_all(void)
{
    int i;

    /* Write FAT for each drive (WRTFAT loop, ASM lines 2495-2503) */
    for (i = 0; i < (int)dos->NUMDRV; i++) {
        byte *bp = dos->DRVTAB[i];
        if (bp)
            fat_check_write(bp);
    }

    /* Flush dirty sector buffer (ASM lines 2504-2515) */
    if (dos->BUFDRVNO != 0xFF && dos->DIRTYBUF) {
        byte *bp = dos->DRVTAB[(int)dos->BUFDRVNO];
        if (bp) {
            dos->DIRTYBUF = 0;
            dwrite(dos->BUFFER, 1, dos->BUFSECNO, bp);
        }
    }
}


/* -----------------------------------------------------------------------
 * fat_allocate — Allocate CX clusters and chain them onto a file.
 *
 * ASM: ALLOCATE  86DOS.asm:2169-2284
 *
 * Inputs:
 *   si   — FAT pointer
 *   bx   — last cluster already in file (0 if file is empty)
 *   cx   — number of clusters to allocate
 *   dx   — chain position of cluster bx
 *   bp   — DPB pointer
 *   fcb  — pointer to FCB (for updating FIRCLUS if file was empty)
 * Outputs:
 *   Returns 0 on success; bx updated to first newly-allocated cluster.
 *   Returns -1 (carry set) if insufficient space; cx = max records addable.
 *   FIRCLUS field of FCB set if file was null (bx was 0 on entry).
 * ----------------------------------------------------------------------- */
int fat_allocate(byte *si, word bx, word cx, word dx, byte *bp, byte *fcb,
                 word *bx_out, word *cx_out)
{
    word maxclus = DPB_GET_WORD(bp, MAXCLUS);
    word orig_bx = bx;          /* saved so we know if file was null        */
    word orig_cx = cx;          /* total clusters requested                 */
    word orig_dx = dx;
    word first_alloc = 0;       /* first newly allocated cluster            */
    word last_link   = bx;      /* cluster we will link the new chain into  */
    word search_up   = bx;      /* upper search pointer                     */
    word search_dn   = bx;      /* lower search pointer (TRYIN path)        */

    /*
     * The ASM uses a bi-directional search: BX searches upward (TRYOUT)
     * and AX searches downward (TRYIN) simultaneously, meeting in the
     * middle to minimise fragmentation.
     *
     * ASM: ALLOC loop (lines 2197-2257).
     */

    /* Preserve first byte of FAT (DIRSIZ flag) — ASM: PUSH [SI] */
    byte fat0_save = si[0];

    while (cx > 0) {
        word prev = last_link;  /* cluster to link this new one into        */
        word found = 0;
        int  found_up = 0;

        /* Bi-directional search for free cluster */
        search_up = prev;
        search_dn = prev;

        for (;;) {
            /* TRYOUT: search upward */
            search_up++;
            if (search_up <= maxclus) {
                if (fat_unpack(si, search_up, bp) == FAT_FREE) {
                    found = search_up;
                    found_up = 1;
                    break;
                }
            }
            /* TRYIN: search downward */
            if (search_dn > 1) {
                search_dn--;
                if (fat_unpack(si, search_dn, bp) == FAT_FREE) {
                    found = search_dn;
                    found_up = 0;
                    break;
                }
            }
            /* Exhausted? */
            if (search_up > maxclus && search_dn <= 1) {
                /*
                 * No free cluster found.  Release what we just allocated,
                 * compute how many records could be added, return error.
                 * ASM: FINDFRE exhausted → lines 2203-2224.
                 */
                /* Partial rollback: free the clusters we allocated */
                if (first_alloc != 0) {
                    fat_relblks(si, first_alloc, FAT_FREE, bp);
                }
                /* Restore FAT[0] */
                si[0] = fat0_save;

                {
                    /* CX = max records that could be added (ASM 2208-2221) */
                    word allocated = (word)(orig_cx - cx); /* how many we got */
                    word pos       = (word)(orig_dx + 1);  /* pos of first new */
                    word clus_in_file = (word)(pos + allocated);
                    word recs_per_clus = (word)((DPB_GET_BYTE(bp, CLUSMSK) + 1));
                    word max_recs  = (word)(clus_in_file * recs_per_clus);
                    word recpos    = LO16(dos->RECPOS);
                    *cx_out = (max_recs > recpos) ? (word)(max_recs - recpos) : 0;
                }
                *bx_out = bx;
                return -1;  /* carry set */
            }
        }

        /* Link previous cluster to found cluster (HAVFRE, ASM 2237-2244) */
        fat_pack(si, prev, found);          /* prev → found                */
        if (first_alloc == 0) first_alloc = found;
        last_link = found;
        cx--;
    }

    /* Terminate chain with EOF (ASM line 2243-2244) */
    fat_pack(si, last_link, FAT_EOF);
    DPB_SET_BYTE(bp, DIRTYFAT, DIRTYFAT_DIRTY);

    /* Restore FAT[0] (DIRSIZ byte) */
    si[0] = fat0_save;

    /* If file was empty, update FIRCLUS in FCB (ASM lines 2249-2256) */
    {
        /* Re-read original first entry to see what it was */
        word old_entry;
        if (orig_bx == 0) {
            old_entry = 0;
        } else {
            old_entry = fat_unpack(si, orig_bx, bp);
            /* If orig_bx had DI=0 (was unlinked), treat as null file */
        }
        /* ASM: UNPACK orig_bx → DI; if DI==0, file was null */
        old_entry = (orig_bx == 0) ? 0 : fat_unpack(si, orig_bx, bp);
        if (old_entry == 0 || orig_bx == 0) {
            FCB_SET_WORD(fcb, FIRCLUS, first_alloc);
        }
    }

    *bx_out = first_alloc;
    *cx_out = 0;
    return 0;   /* carry clear */
}
