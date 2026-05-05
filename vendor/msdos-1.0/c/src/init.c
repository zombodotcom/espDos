/*
 * init.c -- DOS kernel initialisation.
 *
 * ASM labels covered:
 *   DOSINIT     86DOS.asm:3296-3423
 *   PERDRV      86DOS.asm:3306-3423  (per-drive setup loop)
 *   CONTINIT    86DOS.asm:3424-3556  (post-drive-table setup)
 *   MOVFAT      86DOS.asm:3286-3294  (FAT relocation stub)
 *   FININIT     86DOS.asm:3292-3294  (final PSP setup)
 *   FIGFATSIZ   86DOS.asm:3557-3584  (iterative FAT size calculation)
 *   FIGMAX      86DOS.asm:3564-3584  (MAXCLUS computation)
 *   MYD         86DOS.asm:3586-3610  (decimal string -> word)
 *   GETDAT      86DOS.asm:3498-3527  (date prompt)
 *   MEMSCAN     86DOS.asm:3473-3484  (memory size detect)
 *
 * Design notes:
 *   DOSINIT in the original ASM receives a pointer to an init table in
 *   DS:SI.  The table has the format:
 *     byte    NUMDRV          number of logical drives
 *     [for each drive:]
 *       word  ptr_to_dpt      pointer to BIOS Disk Parameter Table
 *   where each DPT has:
 *       word  SECSIZ
 *       byte  SPC             sectors per cluster (raw, not minus 1)
 *       word  FIRFAT          number of reserved sectors
 *       byte  FATCNT
 *       word  MAXENT
 *       word  DSKSIZ          total sectors on disk
 *
 *   In the C translation the table is passed as a byte array; we parse
 *   it with byte-at-a-time reads to mirror the ASM LODB / LODW / MOVB /
 *   MOVW sequences exactly.
 *
 *   The FAT relocation (MOVFAT / CONTINIT) performed in the ASM by
 *   memmove-style block copies is replaced by a plain memmove() call.
 *
 *   MEMSCAN (probe memory by read-modify-verify) has no C equivalent;
 *   we accept ENDMEM from the caller instead.
 *
 *   The date prompt (GETDAT) is preserved as a console interaction using
 *   the existing fn_bufin() / con_out() helpers.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "dos.h"

/* ----------------------------------------------------------------------- */
/* Storage for the global sate block and BIOS vtable                        */
/* (Both are declared extern in their respective headers; syscall.c defines */
/*  them.  init.c must not re-define them.)                                 */
/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/* ADJFAC / MEMSTRT equivalents                                             */
/*                                                                           */
/* In the ASM MEMSTRT is the first byte after the fixed DOS code/data area  */
/* where DOSINIT begins placing DPBs and FATs.  ADJFAC is defined as        */
/* DIRBUF - MEMSTRT (line 3621), where DIRBUF is the 16-bit offset of the   */
/* directory sector buffer within the DOS CS segment.                        */
/*                                                                           */
/* In the C translation we allocate DPBs and FATs from a static pool that   */
/* dos_init() receives from the caller, or from a static array if no pool   */
/* is provided.                                                              */
/* ----------------------------------------------------------------------- */
#define DPB_MAX_FAT_BYTES   (256)   /* conservative upper bound per drive  */
#define DPB_POOL_SIZE       (MAX_DRIVES * (DPB_FIXED_SIZE + DPB_MAX_FAT_BYTES))

static byte dpb_pool[DPB_POOL_SIZE];
static byte sector_buffer[2][512];  /* two sector-sized buffers (MAXSEC each) */
static byte dir_buffer[512];        /* directory sector buffer                 */
static dos_state_t  dos_state;      /* static state block                      */


/* =======================================================================
 * FIGMAX -- compute MAXCLUS from FIRREC and DSKSIZ
 * ASM: FIGMAX  86DOS.asm:3564-3584
 *
 * Inputs:
 *   ax     = FIRREC (first data sector)
 *   bp     = DPB base pointer  (DSKSIZ, CLUSSHFT, SECSIZ fields read)
 * Outputs:
 *   cx_out = MAXCLUS (number of clusters + 1)
 *   al_out = FATSIZ  (number of sectors occupied by FAT)
 *
 * The routine also returns AX = FAT size in sectors (via function return).
 * ======================================================================= */
static word figmax(word ax, byte *bp, word *cx_out)
{
    word dsksiz   = DPB_GET_WORD(bp, MAXCLUS);  /* DSKSIZ at init time     */
    byte clusshft = DPB_GET_BYTE(bp, CLUSSHFT);
    word secsiz   = DPB_GET_WORD(bp, SECSIZ);
    word maxclus, dx;

    /* ASM 3567-3573:
     *   SUB AX,[BP+DSKSIZ]   ; AX = FIRREC - DSKSIZ (negative, then NEG)
     *   NEG AX               ; AX = DSKSIZ - FIRREC = number of data sectors
     *   MOV CL,[BP+CLUSSHFT]
     *   SHR AX,CL            ; AX = number of clusters
     *   INC AX               ; +1 (cluster 0 is unused)
     *   MOV CX,AX            ; CX = MAXCLUS
     */
    ax    = (word)(dsksiz - ax);  /* NEG AX equivalent */
    ax  >>= clusshft;
    ax   += 1;
    maxclus = ax;

    /* ASM 3574-3583:
     *   INC AX
     *   MOV DX,AX
     *   SHR DX               ; DX = (MAXCLUS+1) / 2  (bytes for 12-bit FAT)
     *   ADC AX,DX            ; AX = ceil(MAXCLUS * 1.5) = FAT bytes
     *   MOV SI,[BP+SECSIZ]
     *   ADD AX,SI
     *   DEC AX               ; round up
     *   XOR DX,DX
     *   DIV AX,SI            ; AX = FAT sectors
     */
    ax += 1;
    dx  = ax >> 1;
    ax += dx + (ax & 1 ? 0 : 0);  /* ADC: add carry from SHR */
    /* More precisely: original SHR DX; ADC AX,DX:
     * dx = (maxclus+1) >> 1; AX += dx + carry_from_shr
     * carry_from_shr = (maxclus+1) & 1
     */
    {
        word tmp = maxclus + 1;
        dx = tmp >> 1;
        ax = tmp + dx + (tmp & 1);   /* = ceil(tmp * 3 / 2) */
    }
    ax += secsiz;
    ax -= 1;
    ax /= secsiz;   /* FAT sectors */

    *cx_out = maxclus;
    return ax;   /* FATSIZ */
}


/* =======================================================================
 * figfatsiz -- iterative FAT size calculation
 * ASM: FIGFATSIZ  86DOS.asm:3557-3563  (convergence loop at PERDRV 86DOS.asm:3357-3372)
 *
 * Inputs:  bp = DPB pointer (with SDIRSEC scratch field set)
 *          sdirsec = small-dir sector count (scratch)
 * Outputs: bp->FATSIZ set; returns FATSIZ
 *
 * The loop converges a trial FATSIZ value by iterating FIGFATSIZ until
 * two consecutive iterations agree.
 * ======================================================================= */
static byte figfatsiz_iter(byte *bp, word sdirsec, word *cx_out)
{
    /* ASM: FIGFATSIZ computes:
     *   AX = FATCNT * trial_FATSIZ + FIRFAT + SDIRSEC
     * then calls FIGMAX(AX) to get a new trial.
     */
    word fatcnt  = DPB_GET_BYTE(bp, FATCNT);
    word firfat  = DPB_GET_WORD(bp, FIRFAT);
    byte dl = 1, dh = 0;  /* dl = current trial, dh = previous trial */
    byte al;
    word cx = 0;
    int  iter;

    for (iter = 0; iter < 32; iter++) {
        /* ASM 3559-3563:
         *   MUL AL,[BP+FATCNT]
         *   ADD AX,[BP+FIRFAT]
         *   ADD AX,[SDIRSEC]
         */
        word ax = (word)dl * fatcnt + firfat + sdirsec;
        al = (byte)figmax(ax, bp, &cx);
        if (al == dl) break;        /* converged */
        if (al == dh) {             /* oscillating */
            /* ASM: DEC [BP+DSKSIZ] ; restart */
            word dsksiz = DPB_GET_WORD(bp, MAXCLUS);
            DPB_SET_WORD(bp, MAXCLUS, dsksiz - 1);
            dl = 1;
            dh = 0;
            iter = 0;
            continue;
        }
        dh = dl;
        dl = al;
    }
    *cx_out = cx;
    return al;
}


/* =======================================================================
 * myd -- parse decimal number from string
 * ASM: MYD  86DOS.asm:3586-3610
 *
 * Inputs:
 *   s   = NUL/CR-terminated decimal digit string
 *   max = exclusive upper bound (carry set if result >= max)
 * Outputs:
 *   result in *val_out; returns 0 on success, -1 on error (carry set)
 * ======================================================================= */
static int myd(const byte *s, word max, word *val_out)
{
    word bx = 0;

    while (*s) {
        byte c = *s++;
        if (c < '0' || c > '9') break;
        c = (byte)(c - '0');
        bx = bx * 10 + c;
    }
    *val_out = bx;
    if (bx == 0) return -1;      /* carry set if zero */
    if (bx >= max) return -1;    /* carry set if >= max */
    return 0;
}


/* =======================================================================
 * getdat -- prompt user for today's date, set dos->DATE
 * ASM: GETDAT  86DOS.asm:3498-3527
 *
 * Uses fn_bufin() for line input and parses M-D-Y format.
 * ======================================================================= */
static void getdat(void)
{
    /* ASM 3499-3527: output DATMES, read DATBUF, parse m-d-y */
    static const byte datmes[] = "Enter today's date (m-d-y): $";
    byte datbuf[16];
    word month, day, year;

    for (;;) {
        /* Print prompt */
        con_outmes(datmes);

        /* Read a line (up to 12 chars) */
        datbuf[0] = 12;   /* max length */
        datbuf[1] = 0;
        fn_bufin(datbuf);
        con_crlf();

        /* Parse month (1..12) */
        if (myd(datbuf + 2, 12, &month)  != 0) continue;

        /* Advance past month digits and separator */
        {
            const byte *p = datbuf + 2;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '-' || *p == '/') p++;

            /* Parse day (1..31) */
            if (myd(p, 31, &day) != 0) continue;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '-' || *p == '/') p++;

            /* Parse year (80..99 or 1980..1999) */
            if (myd(p, 2100, &year) != 0) continue;
        }

        /* ASM 3518-3526: subtract 80, check range, fold century */
        if (year >= 1900) year -= 1900;
        year -= 80;
        if ((sword)year < 0) continue;
        if (year > 19) {
            /* ASM: SUB AX,1900 ; JC GETDAT */
            year -= (word)(1900 - 80);   /* effectively year -= 1820 */
            if ((sword)year < 0) continue;
        }

        /* ASM: pack DATE = month<<5 | day; DATE+1 |= year<<1 */
        dos->DATE = (word)((month << 5) | day);
        dos->DATE |= (word)(year << 9);
        break;
    }
}


/* =======================================================================
 * dos_init -- initialise the DOS kernel
 * ASM: DOSINIT  86DOS.asm:3296-3423
 *      CONTINIT  86DOS.asm:3424-3556
 *
 * Inputs:
 *   bios_table  -- pointer to populated BIOS vtable (replaces BIOSSEG stubs)
 *   init_table  -- pointer to the per-drive initialisation table (see above)
 *                  format: byte NUMDRV; then for each drive:
 *                    word ptr_to_dpt  (points to: word SECSIZ, byte SPC,
 *                                      word FIRFAT, byte FATCNT,
 *                                      word MAXENT, word DSKSIZ)
 *   endmem      -- first unavailable segment (replaces MEMSCAN result)
 *
 * Outputs: global 'dos' and 'bios' are initialised; DOS is ready to
 *          accept system calls via dos_dispatch().
 *
 * NOTE: differs from ASM because MEMSCAN (probe-write-verify loop to find
 * top of RAM) and the interrupt-vector setup (writing into the real-mode
 * IVT at segment 0) cannot be expressed in portable C.  The caller must
 * supply ENDMEM directly.  Interrupt vector writes are stubbed.
 * ======================================================================= */
void dos_init(bios_vtable_t *bios_table, byte *init_table)
{
    byte  *si   = init_table;   /* source pointer, mirrors ASM SI */
    byte  *di   = dpb_pool;     /* destination in DPB pool        */
    byte  *es   = dpb_pool;     /* ES base in ASM = CS = DOS seg  */
    int    drv;
    byte   numdrv;
    word   maxsec = 0;
    word   sdirsec = 0;         /* scratch: small-dir sector count */

    /* Attach state and BIOS */
    dos  = &dos_state;
    bios = bios_table;
    memset(dos, 0, sizeof(*dos));

    /* DOSINIT 3301-3303:
     *   LODB            ; AL = NUMDRV from init table
     *   MOV [NUMDRV],AL
     */
    numdrv = *si++;
    dos->NUMDRV = numdrv;

    /* Initialise DRVCNT (ASM DRVCNT label, line 3612: DB 0) */
    /* We use a local variable 'drv' instead */

    /* PERDRV loop: 86DOS.asm:3306-3423 */
    for (drv = 0; drv < numdrv; drv++) {
        byte *bp   = di;    /* DI = start of this DPB */
        byte *dpt;
        word  secsiz, spc, firfat, fatcnt, maxent, dsksiz;
        byte  clusmsk, clusshft;
        word  dirsec_val, cx = 0;
        byte  al;

        /* Store DPB pointer in DRVTAB */
        dos->DRVTAB[drv] = bp;

        /* ASM 3313-3314: STOB ; DRVNUM */
        DPB_SET_BYTE(bp, DRVNUM, (byte)drv);
        di++;   /* advance past DRVNUM byte */

        /* ASM 3315-3317: LODW (ptr to DPT) ; MOV SI,AX */
        {
            word dpt_offset = (word)((si[1] << 8) | si[0]);
            si += 2;
            dpt = init_table + dpt_offset;
        }

        /* ASM 3318-3320: LODW SECSIZ ; STOW ; MOV DX,AX */
        secsiz = (word)((dpt[1] << 8) | dpt[0]);
        dpt += 2;
        DPB_SET_WORD(bp, SECSIZ, secsiz);
        di += 2;
        if (secsiz > maxsec) maxsec = secsiz;

        /* ASM 3327-3338: LODB SPC ; compute CLUSMSK and CLUSSHFT */
        spc = *dpt++;
        clusmsk = (byte)(spc - 1);
        DPB_SET_BYTE(bp, CLUSMSK, clusmsk);
        di++;
        if (clusmsk == 0) {
            clusshft = 0;
        } else {
            byte tmp = clusmsk;
            clusshft = 0;
            while (tmp > 1) { tmp >>= 1; clusshft++; }
        }
        DPB_SET_BYTE(bp, CLUSSHFT, clusshft);
        di++;

        /* ASM 3339: MOVW FIRFAT */
        firfat = (word)((dpt[1] << 8) | dpt[0]);
        dpt += 2;
        DPB_SET_WORD(bp, FIRFAT, firfat);
        di += 2;

        /* ASM 3340: MOVB FATCNT */
        fatcnt = *dpt++;
        DPB_SET_BYTE(bp, FATCNT, (byte)fatcnt);
        di++;

        /* ASM 3341: MOVW MAXENT */
        maxent = (word)((dpt[1] << 8) | dpt[0]);
        dpt += 2;
        DPB_SET_WORD(bp, MAXENT, maxent);
        di += 2;

        /* ASM 3342-3351:
         * Compute DIRSEC = ceil(MAXENT / (SECSIZ/32))
         *   MOV AX,DX       ; AX = SECSIZ
         *   MOV CL,5
         *   SHR AX,CL       ; AX = entries per sector (SECSIZ/32)
         *   MOV CX,AX
         *   DEC AX
         *   ADD AX,[BP+MAXENT]
         *   XOR DX,DX
         *   DIV AX,CX       ; AX = ceil(MAXENT / (SECSIZ/32))
         *   STOW             DIRSEC (temp)
         */
        {
            word eps = secsiz >> 5;   /* entries per sector */
            dirsec_val = (maxent + eps - 1) / eps;
            /* STOW at current DI position = FIRREC offset */
            DPB_SET_WORD(bp, FIRREC, dirsec_val);
            di += 2;  /* advance past FIRREC/DIRSEC word */
        }

        /* ASM 3352-3355: compute SDIRSEC = ceil(DIRSEC/2) */
        sdirsec = (dirsec_val + 1) / 2;

        /* ASM 3356: MOVW DSKSIZ */
        dsksiz = (word)((dpt[1] << 8) | dpt[0]);
        dpt += 2;
        DPB_SET_WORD(bp, MAXCLUS, dsksiz);  /* DSKSIZ stored at MAXCLUS */
        di += 2;

        /* ASM 3357-3373: iterate to find FATSIZ */
        al = figfatsiz_iter(bp, sdirsec, &cx);
        DPB_SET_BYTE(bp, FATSIZ, al);
        di++;

        /* ASM 3374-3379: FIRDIR = FATCNT*FATSIZ + FIRFAT */
        {
            word firdir = (word)(fatcnt * al) + firfat;
            DPB_SET_WORD(bp, FIRDIR, firdir);
            di += 2;

#if SMALLDIR
            /* ASM 3391-3404: compute FIRREC1/MAXCLUS1/FIRREC2/MAXCLUS2 */
            {
                word firrec1, firrec2, maxclus1, maxclus2;
                word cx1 = 0, cx2 = 0;

                /* FIRREC1 = FIRDIR + SDIRSEC */
                firrec1 = firdir + sdirsec;
                DPB_SET_WORD(bp, FIRREC1, firrec1);
                di += 2;

                /* MAXCLUS1 = previously computed cx from figfatsiz_iter */
                maxclus1 = cx;
                DPB_SET_WORD(bp, MAXCLUS1, maxclus1);
                di += 2;

                /* FIRREC2 = FIRDIR + DIRSEC (large directory) */
                firrec2 = firdir + dirsec_val;
                DPB_SET_WORD(bp, FIRREC2, firrec2);
                di += 2;

                /* MAXCLUS2 from FIGMAX(FIRREC2) */
                (void)figmax(firrec2, bp, &cx2);
                maxclus2 = cx2;
                DPB_SET_WORD(bp, MAXCLUS2, maxclus2);
                di += 2;
            }
#else
            /* Non-SMALLDIR: FIRREC = FIRDIR + DIRSEC; compute MAXCLUS */
            {
                word firrec = firdir + dirsec_val;
                DPB_SET_WORD(bp, FIRREC, firrec);
                (void)figmax(firrec, bp, &cx);
                DPB_SET_WORD(bp, MAXCLUS, cx);
            }
#endif
        }

        /* ASM 3407-3408: DIRTYFAT = 0xFF (never read) */
        DPB_SET_BYTE(bp, DIRTYFAT, DIRTYFAT_UNREAD);
        di++;

        /* ASM 3409-3413: advance DI by FATSIZ * SECSIZ to allocate FAT */
        {
            word fatsiz = DPB_GET_BYTE(bp, FATSIZ);
            di += (word)(fatsiz * secsiz);
        }

        /* si already advanced (we read 2 bytes for dpt ptr at top of loop) */
        (void)si;   /* si is now pointing at next drive's DPT ptr */
    }

    /* CONTINIT  86DOS.asm:3424-3556 */

    /* ASM 3425-3434:
     *   LODW AX = max buffer size (from init table, after per-drive entries)
     *   BX = [MAXSEC]
     *   BUFFER = DIRBUF + MAXSEC
     *   DI += MAXSEC  ; allocate directory buffer
     *   DI += MAXSEC  ; allocate sector buffer
     *   DI += ADJFAC+15 ; align to paragraph
     *   SHR DI,4     ; first free segment
     */
    dos->MAXSEC   = maxsec;
    dos->DIRBUF   = dir_buffer;
    dos->BUFFER   = sector_buffer[0];

    /* ASM 3438-3465: set up interrupt vectors (IVT writes) */
    /* NOTE: differs from ASM because writing to the real-mode IVT at
     * segment 0 is not possible in portable C.  We record the important
     * values in the dos state instead. */
    dos->CURDRVPT = dos->DRVTAB[0];
    dos->BUFDRVNO = (byte)-1;    /* -1 = no drive buffered */
    dos->DIRTYBUF = 0;
    dos->DMAADD   = 0x0080;
    dos->DMABASE  = NULL;       /* caller must call fn_setdma before any I/O */

    /* ASM 3473-3484: MEMSCAN -- probe memory to find top of RAM.
     * NOTE: differs from ASM because real-mode probe-write loop cannot be
     * expressed in portable C.  ENDMEM is set to 0 here; caller should
     * update dos->ENDMEM after dos_init() returns. */
    dos->ENDMEM   = 0;

    /* ASM 3485-3492: set [EXIT] = 0x100 in IVT (default exit address),
     * print HEADER message.
     * NOTE: differs from ASM because IVT write is stubbed. */
    {
        static const byte header[] =
            "\r\n86-DOS  version 1.00  (C) 1981  Seattle Computer Products\r\n$";
        con_outmes(header);
    }

    /* ASM 3498-3527: prompt for date */
    getdat();

    /* ASM 3528-3555: relocate FATs to final position.
     * In the ASM the FATs were placed at MEMSTRT (just after DOS code)
     * and need to be moved up by ADJFAC = DIRBUF - MEMSTRT to make room
     * for the two sector buffers.  In the C translation the DPBs and FATs
     * are already in dpb_pool which sits at a fixed address; no relocation
     * needed.
     * NOTE: differs from ASM because flat C memory layout does not require
     * the memmove that was necessary in the real-mode segment model. */

    /* Initialise DIRBUFID to "invalid" */
    dos->DIRBUFID   = (word)-1;
    dos->BUFSECNO   = 0;
}
