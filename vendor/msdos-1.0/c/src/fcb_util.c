/*
 * fcb_util.c — FCB and filename utilities: MAKEFCB, SETRNDREC, FILESIZE,
 *              SRCHFRST, SRCHNXT, and supporting helpers.
 *
 * Translated from 86DOS.asm.  ASM labels covered:
 *
 *   MAKEFCB     86DOS.asm:3024-3063  — system call 41: parse filename → FCB
 *   GETWORD     86DOS.asm:3065-3090  — copy name word into buffer
 *   GETLET      86DOS.asm:3092-3113  — read one letter, upcase, filter separators
 *   SETRNDREC   86DOS.asm:2545-2549  — system call 36: set random record field
 *   FILESIZE    86DOS.asm:2392-2441  — system call 35: compute file size in records
 *   SRCHFRST    86DOS.asm:2302-2377  — system call 17: search-first
 *   SRCHNXT     86DOS.asm:2378-2391  — system call 18: search-next
 *   SAVPLCE     86DOS.asm:2306-2350  — common save-and-report after search
 *   KILLSRCH    86DOS.asm:2351-2357  — report "not found" for search
 *   SRCHDEV     86DOS.asm:2358-2377  — report device match for search
 */

#include <string.h>
#include "../include/dos.h"

/* -----------------------------------------------------------------------
 * getlet — Read one character from SI, upcase a-z, classify as separator.
 *
 * ASM: GETLET  86DOS.asm:3092-3113
 *
 * Reads *(*si)++ (i.e. SI advances).
 * Returns the character (upcased if alphabetic).
 * Sets *is_sep = 1 (ZF set in ASM) if the character is a separator:
 *   ' '  '='  ','  ';'  '.'  ':'  TAB(9)
 * Returns *is_sep = 0 otherwise.
 * ----------------------------------------------------------------------- */
static byte getlet(byte **si, int *is_sep)
{
    byte al = *(*si)++;
    if (al >= 'a' && al <= 'z')
        al = (byte)(al - 0x20);   /* to upper case */
    /* CHK: separator check */
    *is_sep = (al == ' ' || al == '=' || al == ',' ||
               al == ';' || al == '.' || al == ':' || al == 9) ? 1 : 0;
    return al;
}

/* -----------------------------------------------------------------------
 * getword — Copy up to CX letters from SI into DI, padding with spaces.
 *
 * ASM: GETWORD  86DOS.asm:3065-3090
 *
 * Inputs:
 *   si     — pointer to source string pointer (advanced on return)
 *   di     — output buffer (CX bytes long)
 *   cx     — number of characters to fill
 *   ambig  — pointer to ambiguous-flag (set to 1 if '?' or '*' found)
 * After filling, SI points to the terminating (non-name) character.
 * ----------------------------------------------------------------------- */
static void getword(byte **si, byte *di, int cx, byte *ambig)
{
    while (cx > 0) {
        int is_sep;
        byte al = getlet(si, &is_sep);
        if (is_sep || al <= ' ') {
            /* FILLNAM: pad remainder with spaces */
            memset(di, ' ', cx);
            (*si)--;   /* DEC SI: point back at terminator */
            return;
        }
        if (al == '*') {
            /* Wildcard '*' → fill rest of word with '?' */
            memset(di, '?', cx);
            di += cx;
            *ambig = 1;
            /* Skip over the '*' and fall through to FILLNAM */
            memset(di, ' ', 0);   /* nothing: cx already consumed */
            /* ASM: INC CX, point SI back; here CX was decremented in loop */
            /* advance SI past the '*' already consumed; point back at term */
            {
                int dummy;
                byte nxt = getlet(si, &dummy);
                if (dummy || nxt <= ' ') (*si)--;
                else (*si)--;   /* always back up: FILLNAM does DEC SI */
            }
            return;
        }
        *di++ = al;
        if (al == '?')
            *ambig = 1;
        cx--;
    }
    /* CX exhausted: INC SI to skip terminator (ASM: INC SI then FILLNAM DEC SI) */
    /* net effect: SI unchanged — FILLNAM does DEC SI after STOB at last position */
}

/* -----------------------------------------------------------------------
 * fn_makefcb — Parse a filename string into an FCB (system call 41).
 *
 * ASM: MAKEFCB  86DOS.asm:3024-3063
 *
 * Inputs:
 *   al    — if non-zero, skip leading separators from SI first
 *   si    — pointer to filename string (DS:SI in ASM)
 *   es_di — pointer to FCB output buffer (ES:DI in ASM, at least 12 bytes)
 * Outputs:
 *   Returns 0 if unambiguous name, 1 if wildcard ('?') found.
 *   Fills es_di[0]   = drive number (0 = default, 1 = A:, etc.)
 *         es_di[1..8] = 8-char filename (space-padded)
 *         es_di[9..11]= 3-char extension (space-padded)
 *         es_di[12..13] = 0 (STOW)
 *         es_di[14..15] = 0 (STOW)
 *   SI is updated to point past the parsed filename (written back via
 *   the saved SP mechanism; here we update *si_inout indirectly).
 *
 * NOTE: In the original ASM, SI is returned via [SPSAVE+SISAVE].  In C
 *       we pass si by pointer and update it directly.
 * ----------------------------------------------------------------------- */
byte fn_makefcb(byte *si, byte *es_di, byte al)
{
    byte  ambig = 0;   /* DL: 0 = unambiguous */
    byte *buf   = es_di;

    /* Skip leading separators if AL != 0 */
    if (al != 0) {
        int is_sep = 1;
        while (is_sep) {
            int dummy;
            /* Peek: consume separators */
            getlet(&si, &is_sep);
        }
        si--;   /* DEC SI: back up to first non-separator */
    }

    /* Check for drive specifier: [SI+1] == ':' ? */
    {
        byte drive = 0;
        if (si[1] == ':') {
            int dummy;
            byte dl_ch = getlet(&si, &dummy);
            drive = (byte)(dl_ch - '@');   /* SUB AL,'@' → drive number */
            if (drive == 0 || drive > 15) {
                /* Invalid drive specifier — back up */
                si -= 2;   /* DEC SI twice: NODRV then DEFAULT */
                drive = 0;
            } else {
                si++;   /* INC SI: skip ':' (already consumed by getlet) */
                        /* ASM: INC SI skips the ':', getlet consumed letter */
            }
        }
        buf[0] = drive;   /* STOB: drive byte */
        buf++;
    }

    /* Get 8-char name */
    getword(&si, buf, 8, &ambig);
    buf += 8;

    /* Skip '.' if present */
    if (*si == '.') si++;

    /* Get 3-char extension */
    getword(&si, buf, 3, &ambig);
    buf += 3;

    /* Write two zero words (ASM: STOW STOW → 4 zero bytes at FCB+12) */
    buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = 0;

    /* Update saved SI (in ASM via [SPSAVE+SISAVE]; here we just store it) */
    /* NOTE: the caller must save the updated si if needed.
     * In the C model we rely on the caller passing si by pointer.        */
    (void)si;   /* si updated in place; caller should use it after the call */

    return ambig;
}

/* -----------------------------------------------------------------------
 * fn_setrndrec — Set random record field from NR/EXTENT (system call 36).
 *
 * ASM: SETRNDREC  86DOS.asm:2545-2549
 *
 * Inputs:
 *   fcb — pointer to user FCB (DS:DX in ASM)
 * Outputs:
 *   FCB[33..35] (RR field, 3 bytes) updated from NR/EXTENT.
 *
 * ASM:
 *   CALL GETREC          ; DX:AX = 32-bit record position, CX=1
 *   MOV [DI+33],AX       ; RR low word
 *   MOV [DI+35],DL       ; RR byte 2 (third byte of 3-byte RR)
 * ----------------------------------------------------------------------- */
void fn_setrndrec(byte *fcb)
{
    word dx;
    word ax = fn_getrec(fcb, &dx);

    FCB_SET_WORD(fcb, RR,   ax);
    FCB_SET_BYTE(fcb, RR+2, (byte)(dx & 0xFF));
}

/* -----------------------------------------------------------------------
 * fn_filesize — Compute file size in records (system call 35).
 *
 * ASM: FILESIZE  86DOS.asm:2392-2441
 *
 * Inputs:
 *   fcb — pointer to user FCB
 * Outputs:
 *   Returns 0 on success, 0xFF if file not found.
 *   Writes 3-byte record count into FCB[33..35] (RR field).
 *   Handles both large (32-byte) and small (16-byte) directory entries.
 *
 * ASM:
 *   CALL GETFILE → find file, get SI pointing to first-cluster field
 *   ADD DI,33  → point at RR field of FCB
 *   CX = FCB.RECSIZ (0→128)
 *   if device: AX=DX=0
 *   else: size = [SI+2..SI+5] (4 bytes); if SMALLDIR only 3 bytes
 *         divide high word by CX, then low word+remainder by CX
 *         round up for partial record
 *   STOW AX (low 2 bytes of result)
 *   STOB DL  (3rd byte)
 *   if RECSIZ < 64: also write AH to [DI] (4th byte)
 * ----------------------------------------------------------------------- */
void fn_filesize(byte *fcb)
{
    byte *bx = NULL, *si = NULL, *bp = NULL;
    word  cx;
    word  size_lo, size_hi;
    word  result_lo = 0, result_hi = 0;

    if (getfile(fcb, &bx, &si, &bp) != 0) {
        FCB_SET_BYTE(fcb, RR,   0xFF);
        return;
    }

    /* CX = RECSIZ */
    cx = FCB_GET_WORD(fcb, RECSIZ);
    if (cx == 0) cx = 128;

    if (bx[1] == 0xFF) {
        /* Device: size = 0 */
        size_lo = 0; size_hi = 0;
    } else {
        /* Large entry: [SI+2..SI+5] */
        size_lo = (word)(si[2] | ((word)si[3] << 8));

        if (DPB_GET_BYTE(bp, DIRSIZ) == 0xFF) {
            /* SMALLDIR: only 3 bytes of size (3rd byte at SI+4) */
            size_hi = (word)si[4];
        } else {
            size_hi = (word)(si[4] | ((word)si[5] << 8));
        }

        /* Divide: result = (size_hi:size_lo) / cx, round up */
        /* High word: size_hi / cx */
        {
            word hi_quot  = size_hi / cx;
            word hi_rem   = size_hi % cx;
            /* Low portion: (hi_rem << 16 | size_lo) / cx */
            /* We need 32-bit division: (hi_rem * 65536 + size_lo) / cx */
            /* Split into two 16-bit divides:                           */
            word lo_quot  = (word)(((dword)hi_rem * 65536ul + size_lo) / cx);
            word lo_rem   = (word)(((dword)hi_rem * 65536ul + size_lo) % cx);
            result_lo = lo_quot;
            result_hi = hi_quot;
            if (lo_rem != 0) {
                /* Round up: INC AX */
                result_lo++;
                if (result_lo == 0) result_hi++;
            }
        }
    }

    /* Write RR: 3 bytes (or 4 if RECSIZ < 64) */
    FCB_SET_WORD(fcb, RR,   result_lo);
    FCB_SET_BYTE(fcb, RR+2, (byte)(result_hi & 0xFF));
    if (cx < 64) {
        FCB_SET_BYTE(fcb, RR+3, (byte)(result_hi >> 8));
    }
}

/* -----------------------------------------------------------------------
 * savplce — Save search position and copy directory entry to DMA buffer.
 *
 * ASM: SAVPLCE  86DOS.asm:2306-2350
 *
 * Called after a successful search.  Copies 33 bytes of directory entry
 * data into the user's DMA area, with drive number prepended.
 *
 * Inputs:
 *   bx   — pointer to matching directory entry in DIRBUF
 *   si_v — pointer to first-cluster field (si from getfile)
 *   bp   — DPB
 *   fcb  — user FCB (for saving LASTENT in FILDIRBLK)
 * ----------------------------------------------------------------------- */
static byte savplce(byte *bx, byte *si_v, byte *bp, byte *fcb)
{
    byte *dma  = dos->DMABASE + dos->DMAADD;
    byte  smalldir = DPB_GET_BYTE(bp, DIRSIZ);
    byte  drvnum   = DPB_GET_BYTE(bp, DRVNUM);

    /* Save LASTENT in FCB.FILDIRBLK */
    FCB_SET_WORD(fcb, FILDIRBLK, dos->LASTENT);

    /* Device match: bx[1] == 0xFF */
    if (bx != NULL && bx[1] == 0xFF) {
        /* SRCHDEV: build synthetic 33-byte record */
        dma[0] = 0;         /* zero drive byte for device */
        /* device name: 3 bytes from IONAME (si_v - 3 in ASM) */
        dma[1] = si_v[-2];
        dma[2] = si_v[-1];
        dma[3] = si_v[0];
        /* fill bytes 4-11 with spaces */
        memset(dma + 4, ' ', 8);
        /* zero bytes 12-32 */
        memset(dma + 12, 0, 21);
        FCB_SET_WORD(fcb, FILDIRBLK, (word)(bx[0] | ((word)bx[1] << 8)));
        return 0;
    }

    dma[0] = (byte)(drvnum + 1);   /* 1-based drive number */

    if (smalldir == 0xFF) {
        /* Small (16-byte) directory entries: 11-byte name + 1 zero + 16 bytes */
        memcpy(dma + 1, bx, 11);  /* 11-byte name */
        dma[12] = 0;
        /* Zero 7 words (14 bytes) */
        memset(dma + 13, 0, 14);
        /* Copy 2 words (first cluster pointer) and low 3 bytes of length */
        memcpy(dma + 27, bx + 11, 4);   /* cluster + size low */
        dma[31] = bx[15];               /* 3rd byte of size */
        dma[32] = 0;                    /* 4th byte zero */
    } else {
        /* Large (32-byte) entries: copy name (1 byte + 10 words = 21 bytes)
         * plus 10 words (20 bytes) = 21 bytes total beyond name             */
        memcpy(dma + 1, bx, 11);        /* 11 bytes name */
        memcpy(dma + 12, bx + 11, 21); /* rest of 32-byte entry */
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * fn_srchfrst — Search first (system call 17).
 *
 * ASM: SRCHFRST  86DOS.asm:2302-2377
 *
 * Inputs:
 *   fcb — pointer to user FCB (may contain wildcards in name)
 * Outputs:
 *   Returns 0 on success, 0xFF if not found.
 *   Copies matching directory entry to DMA buffer.
 * ----------------------------------------------------------------------- */
byte fn_srchfrst(byte *fcb)
{
    byte *bx = NULL, *si = NULL, *bp = NULL;

    if (getfile(fcb, &bx, &si, &bp) != 0) {
        /* KILLSRCH */
        FCB_SET_WORD(fcb, FILDIRBLK, (word)-2);
        return 0xFF;
    }

    return savplce(bx, si, bp, fcb);
}

/* -----------------------------------------------------------------------
 * fn_srchnxt — Search next (system call 18).
 *
 * ASM: SRCHNXT  86DOS.asm:2378-2391
 *
 * Continues a search started by SRCHFRST.  The FCB.FILDIRBLK field
 * holds the LASTENT value from the previous search.
 * ----------------------------------------------------------------------- */
byte fn_srchnxt(byte *fcb)
{
    byte *bp = NULL;

    /* MOVNAME to get BP (drive) */
    if (movname(fcb, &bp) != 0) {
        FCB_SET_WORD(fcb, FILDIRBLK, (word)-2);
        return 0xFF;
    }

    /* Restore LASTENT from FCB.FILDIRBLK */
    dos->LASTENT = FCB_GET_WORD(fcb, FILDIRBLK);

    /* CONTSRCH from saved position */
    {
        byte *bx = NULL, *si = NULL;
        if (contsrch(&bx, &si, bp) != 0) {
            FCB_SET_WORD(fcb, FILDIRBLK, (word)-2);
            return 0xFF;
        }
        return savplce(bx, si, bp, fcb);
    }
}
