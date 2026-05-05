/*
 * file.c — File open, close, create, delete, rename system calls.
 *
 * Translated from 86DOS.asm.  The following ASM labels are covered here:
 *
 *   OPEN        86DOS.asm:707-845   — system call 15: open file
 *   DOOPEN      86DOS.asm:711-756   — common open path (also used by CREATE)
 *   OPENDEV     86DOS.asm:757-845   — open an I/O device
 *   CLOSE       86DOS.asm:846-972   — system call 16: close file
 *   BADCLOSE    86DOS.asm:946-972   — close error path
 *   CREATE      86DOS.asm:973-1094  — system call 22: create file
 *   EXISTENT    86DOS.asm:999-1030  — truncate existing file on create
 *   FREESPOT    86DOS.asm:1032-1061 — initialise new directory entry
 *   WRTBACK     86DOS.asm:1062-1068 — write directory and call DOOPEN
 *   DELETE      86DOS.asm:602-625   — system call 19: delete file(s)
 *   RENAME      86DOS.asm:626-659   — system call 23: rename file(s)
 *   ERRET       86DOS.asm:655-659   — common error return (AL = -1)
 */

#include <string.h>
#include "../include/dos.h"

/* -----------------------------------------------------------------------
 * Internal helper — device open path.
 *
 * ASM: OPENDEV  86DOS.asm:757-845
 *
 * When the matched directory "entry" is actually an I/O device, we only
 * store BX (the device indicator word) into the FCB FILDIRBLK field.
 *
 * Inputs:
 *   fcb  — pointer to the user FCB (ES:DI in ASM)
 *   bx   — the device sentinel value (BX from FINDNAME; BH = 0xFF)
 * ----------------------------------------------------------------------- */
static void opendev(byte *fcb, byte *bx)
{
    /* SEG ES  MOV [DI+FILDIRBLK],BX */
    FCB_SET_WORD(fcb, FILDIRBLK, (word)(bx[0] | ((word)bx[1] << 8)));
    /* XOR AL,AL  — success, AL already 0 in caller */
}

/* -----------------------------------------------------------------------
 * doopen — Fill FCB from matching directory entry.
 *
 * ASM: DOOPEN  86DOS.asm:711-756
 *
 * This is entered both from OPEN and from CREATE (after writing the new
 * directory entry) with CF clear.  It fills all the internal FCB fields
 * from the directory data.
 *
 * Inputs:
 *   fcb   — pointer to the user FCB
 *   bx    — pointer to start of directory entry in DIRBUF
 *   si    — pointer to First Cluster field in directory entry
 *   bp    — pointer to DPB
 * Outputs:
 *   Returns 0 on success, -1 if bx indicates device (BH == 0xFF).
 *
 * ASM register usage (DS=ES=CS, DI=FCB):
 *   AL = DRVNUM + 1   → FCB[0]  (drive letter 1-based)
 *   DI += 11          (skip name, point at EXTENT)
 *   [DI] = 0, [DI+2] = 0   (extent = 0)
 *   [DI+2] = 128            (RECSIZ = 128, default CP/M record size)
 *   AX = [SI]         (first cluster)   DX = AX
 *   [DI+4..7] = [SI+2..5]   (file size, 4 bytes)
 *   AX = [SI-8]       (date field in directory)
 *   (SMALLDIR: if DIRSIZ==-1 zero out date)
 *   [DI+8] = AX       (date stored in FCB as FDATE)
 *   [DI+10] = LASTENT (directory block number)
 *   [DI+12] = DX      (first cluster = FIRCLUS)
 *   [DI+14] = DX      (last cluster accessed = LSTCLUS)
 *   [DI+16] = 0       (cluster position = CLUSPOS)
 *   [DI+17] = 0       (dirty flag = DIRTYFIL)
 *
 * NOTE: The byte layout above matches fcb.h field offsets relative to
 *       the internal FCB area (i.e. starting at FCB[11] = extent).
 *       The FCB_SET_* macros use absolute offsets from fcb[0].
 * ----------------------------------------------------------------------- */
static byte doopen(byte *fcb, byte *bx, byte *si, byte *bp)
{
    byte  smalldir;
    word  first_cluster, date_val;
    word  filsiz_lo, filsiz_hi;

    /* Check for device (BH == 0xFF from FINDNAME device path) */
    if (bx != NULL && bx[1] == 0xFF) {
        opendev(fcb, bx);
        return 0;
    }

    smalldir = DPB_GET_BYTE(bp, DIRSIZ);

    /* Drive number (1-based) into FCB[0] */
    fcb[0] = DPB_GET_BYTE(bp, DRVNUM) + 1;

    /* FCB internal fields start at offset 11 (after 11-byte name).
     * We address them using the FCB_* offset macros.              */

    /* EXTENT = 0  (word at FCB offset 12 in CP/M terms, but our
     * FCB layout: EXTENT is at FCB_NAME_LEN = 11 from FCB base)  */
    FCB_SET_WORD(fcb, EXTENT,  0);
    /* RECSIZ = 128 (default record size) */
    FCB_SET_WORD(fcb, RECSIZ,  128);

    /* First cluster (word at [SI]) */
    first_cluster = (word)(si[0] | ((word)si[1] << 8));

    /* File size: 4 bytes at SI+2..SI+5 → FILSIZ field in FCB */
    filsiz_lo = (word)(si[2] | ((word)si[3] << 8));
    filsiz_hi = (word)(si[4] | ((word)si[5] << 8));
    FCB_SET_WORD(fcb, FILSIZ,   filsiz_lo);
    FCB_SET_WORD(fcb, FILSIZ+2, filsiz_hi);

    /* Date: word at SI-8 in the directory entry */
    if (smalldir == 0xFF) {
        /* SMALLDIR: 16-byte entries have no date field → zero it */
        date_val = 0;
        FCB_SET_BYTE(fcb, FDATE-1, 0);   /* zero pad byte before date */
    } else {
        date_val = (word)(si[-8] | ((word)si[-7] << 8));
        FCB_SET_WORD(fcb, FDATE-2, date_val);  /* ASM: [SI-2] = date */
    }
    FCB_SET_WORD(fcb, FDATE, date_val);

    /* Directory location (LASTENT → FILDIRBLK) */
    FCB_SET_WORD(fcb, FILDIRBLK, dos->LASTENT);

    /* First cluster */
    FCB_SET_WORD(fcb, FIRCLUS, first_cluster);
    /* Last cluster accessed = first cluster (start of chain) */
    FCB_SET_WORD(fcb, LSTCLUS, first_cluster);
    /* Position of last cluster = 0 */
    FCB_SET_WORD(fcb, CLUSPOS, 0);
    /* Dirty flag = 0 */
    FCB_SET_BYTE(fcb, DIRTYFIL, 0);

    return 0;
}

/* -----------------------------------------------------------------------
 * fn_open — Open a file by FCB name.
 *
 * ASM: OPEN  system call 15  86DOS.asm:707-845
 *
 * Inputs:
 *   fcb — pointer to user FCB (DS:DX in the original; first byte = drive,
 *          next 11 = name)
 * Outputs:
 *   Returns 0 on success, 0xFF (-1 as byte) on error.
 *
 * ASM:
 *   PUSH DX / PUSH DS          ; save FCB pointer on stack
 *   CALL GETFILE               ; find file in directory
 *   POP ES / POP DI            ; restore FCB pointer → ES:DI
 *   JC   ERRET
 *   (fall through to DOOPEN)
 * ----------------------------------------------------------------------- */
byte fn_open(byte *fcb)
{
    byte *bx = NULL, *si = NULL, *bp = NULL;

    if (getfile(fcb, &bx, &si, &bp) != 0)
        return 0xFF;   /* ERRET */

    return doopen(fcb, bx, si, bp);
}

/* -----------------------------------------------------------------------
 * fn_close — Close a file, writing back directory entry if dirty.
 *
 * ASM: CLOSE  system call 16  86DOS.asm:846-972
 *
 * Inputs:
 *   fcb — pointer to user FCB (DS:DX in original)
 * Outputs:
 *   Returns 0 on success, 0xFF on error.
 *
 * ASM logic:
 *   DI = DX  (point at FCB)
 *   if [DI+FILDIRBLK] == -1 → it's a device, return 0 (OKRET1)
 *   if [DI+DIRTYFIL] == 0   → not written, return 0 (OKRET1)
 *   GETBP from FCB[0]
 *   if dirty buffer on same drive → write it back
 *   GETFILE to re-find the file in directory
 *   verify LASTENT == FCB.FILDIRBLK
 *   copy FIRCLUS, FILSIZ, FDATE from FCB → directory entry
 *   DIRWRITE
 *   CHKFATWRT
 * ----------------------------------------------------------------------- */
byte fn_close(byte *fcb)
{
    byte *bx = NULL, *si = NULL, *bp = NULL;
    word  fildirblk;
    word  cur_lastent;
    word  firclus, filsiz_lo, filsiz_hi, fdate;

    /* Check for I/O device */
    fildirblk = FCB_GET_WORD(fcb, FILDIRBLK);
    if (fildirblk == 0xFFFF)
        return 0;   /* OKRET1: can't close I/O device */

    /* Check if written */
    if (FCB_GET_BYTE(fcb, DIRTYFIL) == 0)
        return 0;   /* OKRET1: not modified */

    /* Get drive DPB */
    {
        byte drive = fcb[0];  /* 1-based */
        bp = getbp((byte)(drive - 1));
        if (bp == NULL)
            goto badclose;
    }

    /* Write back dirty sector buffer if on same drive */
    {
        byte drvnum  = DPB_GET_BYTE(bp, DRVNUM);
        byte bufdrvno = dos->BUFDRVNO;
        /* AH=1 in ASM: compare AX=[BUFDRVNO] where AH=drvnum,AL=1 dirty */
        if (drvnum == bufdrvno && dos->DIRTYBUF) {
            /* Write the sector buffer back */
            dos->DIRTYBUF = 0;
            dwrite(dos->BUFFER, 1, dos->BUFSECNO, bp);
        }
    }

    /* Re-find the file to get current directory entry */
    if (getfile(fcb, &bx, &si, &bp) != 0)
        goto badclose;

    cur_lastent = dos->LASTENT;

    /* Verify directory location matches what was recorded at open */
    if (cur_lastent != FCB_GET_WORD(fcb, FILDIRBLK))
        goto badclose;

    /* Copy FCB data back into directory entry via SI */
    firclus   = FCB_GET_WORD(fcb, FIRCLUS);
    filsiz_lo = FCB_GET_WORD(fcb, FILSIZ);
    filsiz_hi = FCB_GET_WORD(fcb, FILSIZ + 2);
    fdate     = FCB_GET_WORD(fcb, FDATE);

    /* [SI] = FIRCLUS */
    si[0] = (byte)(firclus & 0xFF);
    si[1] = (byte)(firclus >> 8);
    /* [SI+2] = FILSIZ lo, [SI+4] = FILSIZ hi */
    si[2] = (byte)(filsiz_lo & 0xFF);
    si[3] = (byte)(filsiz_lo >> 8);

    {
        byte smalldir = DPB_GET_BYTE(bp, DIRSIZ);
        if (smalldir == 0xFF) {
            /* Small entry: only low byte of high word */
            si[4] = (byte)(filsiz_hi & 0xFF);  /* MOV [SI+4],DL */
            /* No date in small entries */
        } else {
            /* Large entry */
            si[4] = (byte)(filsiz_hi & 0xFF);
            si[5] = (byte)(filsiz_hi >> 8);
            /* date at SI-2 */
            si[-2] = (byte)(fdate & 0xFF);
            si[-1] = (byte)(fdate >> 8);
        }
    }

    dirwrite(0, bp);   /* DIRWRITE — AL=0 (not used as sector no. here; DIRWRITE uses DIRBUFID) */
    fat_check_write(bp);
    return 0;

badclose:
    /* BADCLOSE: clear dirty FAT flag, return error */
    DPB_SET_BYTE(bp, DIRTYFAT, 0);
    return 0xFF;
}

/* -----------------------------------------------------------------------
 * fn_create — Create a file (truncating it if it exists).
 *
 * ASM: CREATE  system call 22  86DOS.asm:973-1094
 *
 * Inputs:
 *   fcb — pointer to user FCB
 * Outputs:
 *   Returns 0 on success, 0xFF on error.
 *
 * ASM logic:
 *   MOVNAME → NAME1, BP
 *   If "?" in name → error (wildcard not allowed in CREATE)
 *   FINDNAME
 *   if found → EXISTENT (truncate)
 *   else → find free directory slot
 *         → FREESPOT (init entry from NAME1)
 *   → WRTBACK → DIRWRITE → DOOPEN
 * ----------------------------------------------------------------------- */
byte fn_create(byte *fcb)
{
    byte *bp = NULL;
    byte *bx = NULL, *si = NULL;
    int   i;

    /* MOVNAME */
    if (movname(fcb, &bp) != 0)
        return 0xFF;   /* ERRET3 */

    /* Check for "?" wildcard in name (not allowed in CREATE) */
    for (i = 0; i < 11; i++) {
        if (dos->NAME1[i] == '?')
            return 0xFF;   /* ERRET3 */
    }

    /* FINDNAME: look for existing file */
    if (findname(&bx, &si, &bp) == 0) {
        /* EXISTENT: file already exists, truncate it */

        if (bx[1] == 0xFF) {
            /* Device — just open it */
            goto openjmp;
        }

        {
            word old_cluster;
            /* Zero file size: [SI+2..SI+5] = 0 */
            si[2] = 0; si[3] = 0; si[4] = 0; si[5] = 0;

            /* Write current date */
            {
                byte smalldir = DPB_GET_BYTE(bp, DIRSIZ);
                if (smalldir != 0xFF) {
                    /* Large entries: write date at SI-2 */
                    si[-2] = (byte)(dos->DATE & 0xFF);
                    si[-1] = (byte)(dos->DATE >> 8);
                    si[4]  = 0; si[5] = 0;
                } else {
                    /* Small entries: only byte at SI+4 */
                    si[4] = 0;
                }
            }

            /* Swap old first cluster with zero, then release chain */
            old_cluster = (word)(si[0] | ((word)si[1] << 8));
            si[0] = 0; si[1] = 0;   /* XCHG CX,[SI] with CX=0 */

            if (old_cluster != 0 && old_cluster <= DPB_GET_WORD(bp, MAXCLUS)) {
                byte *fat_si = DPB_FAT_PTR(bp);
                fat_release(fat_si, old_cluster, bp);
                fat_write(bp);
            }
        }

        /* WRTBACK */
        dirwrite(0, bp);
        goto openjmp;
    }

    /* File not found: find a free directory slot */
    startsrch(bp);
    if (getentry(bp, &bx, NULL) != 0)
        return 0xFF;   /* no free slot and no room */

    for (;;) {
        if (bx[0] == 0xE5u)
            break;   /* FREESPOT: free entry */
        {
            byte al_sec = 0;
            byte *dx_lim = dos->DIRBUF + DPB_GET_WORD(bp, SECSIZ);
            if (nextentry(bp, &bx, dx_lim, &al_sec) != 0)
                return 0xFF;
        }
    }

    /* FREESPOT: initialise new directory entry from NAME1 */
    {
        byte *di = bx;
        byte  smalldir = DPB_GET_BYTE(bp, DIRSIZ);
        int   j;

        /* Copy 11-byte name */
        for (j = 0; j < 11; j++)
            di[j] = dos->NAME1[j];

        si = di;   /* SI now points at the entry (first cluster is at offset 15 for large) */

        if (smalldir == 0xFF) {
            /* Small (16-byte) entry: 5 zero bytes after name */
            for (j = 11; j < 16; j++) di[j] = 0;
            /* SI for small dir: si = bx (no offset) */
        } else {
            /* Large (32-byte) entry: 13 zero bytes, then date, then 6 zero bytes */
            for (j = 11; j < 24; j++) di[j] = 0;
            di[24] = (byte)(dos->DATE & 0xFF);
            di[25] = (byte)(dos->DATE >> 8);
            for (j = 26; j < 32; j++) di[j] = 0;
            /* SI points to first cluster field at offset 15 for large entries */
            si = bx + 15;
        }
    }

    dos->DIRTYDIR = 0xFF;
    dirwrite(0, bp);

openjmp:
    /* CLC: clear carry, then jump to DOOPEN */
    return doopen(fcb, bx, si, bp);
}

/* -----------------------------------------------------------------------
 * fn_delete — Delete file(s) matching FCB name (wildcards allowed).
 *
 * ASM: DELETE  system call 19  86DOS.asm:602-625
 *
 * Inputs:
 *   fcb — pointer to user FCB
 * Outputs:
 *   Returns 0 on success, 0xFF if file not found or is a device.
 *
 * ASM:
 *   CALL GETFILE
 *   JC   ERRET
 *   CMP  BH,-1  → ERRET if device
 *   DELFILE loop:
 *     set DIRTYDIR = -1
 *     [BX] = 0E5H  (mark entry deleted)
 *     BX = [SI]    (first cluster from dir entry)
 *     if BX != 0 and BX <= MAXCLUS → RELEASE chain
 *     CALL CONTSRCH  (continue looking for more matches)
 *     JNC  DELFILE
 *   FATWRT
 *   CHKDIRWRITE
 *   XOR AL,AL  → return 0
 * ----------------------------------------------------------------------- */
byte fn_delete(byte *fcb)
{
    byte *bx = NULL, *si = NULL, *bp = NULL;

    if (getfile(fcb, &bx, &si, &bp) != 0)
        return 0xFF;   /* ERRET */

    /* Check for device */
    if (bx[1] == 0xFF)
        return 0xFF;   /* ERRET: can't delete device */

    do {
        word cluster;

        dos->DIRTYDIR = 0xFF;
        bx[0] = 0xE5;   /* mark entry as deleted */

        /* Get first cluster and release FAT chain */
        cluster = (word)(si[0] | ((word)si[1] << 8));
        if (cluster != 0 && cluster <= DPB_GET_WORD(bp, MAXCLUS)) {
            byte *fat_si = DPB_FAT_PTR(bp);
            fat_release(fat_si, cluster, bp);
        }

    } while (contsrch(&bx, &si, bp) == 0);

    fat_write(bp);
    chkdirwrite(bp);
    return 0;
}

/* -----------------------------------------------------------------------
 * fn_rename — Rename file(s) matching FCB name (wildcards allowed).
 *
 * ASM: RENAME  system call 23  86DOS.asm:626-659
 *
 * The user FCB for RENAME has a special layout: the normal 11-byte name
 * (offset 1-11) is the old name; at offset 17 (= 1 + 11 + 5 pad) is the
 * new 11-byte name.
 *
 * Inputs:
 *   fcb — pointer to user FCB
 * Outputs:
 *   Returns 0 on success, 0xFF on error.
 *
 * ASM:
 *   CALL MOVNAME           ; parse old name → NAME1, get BP
 *   JC   ERRET
 *   CMP  BH,-1 → ERRET     ; device can't be renamed
 *   ADD  SI,5              ; SI = DS:DX+1+11+5 = offset of new name
 *   MOV  DI,NAME2
 *   CALL LODNAME           ; parse new name → NAME2
 *   CALL FINDNAME          ; find first match of old name
 *   JC   ERRET
 *   RENFIL loop:
 *     DIRTYDIR = -1
 *     copy NAME2 → [BX], respecting '?' (keep old char if new is '?')
 *     CALL CONTSRCH
 *     JNC  RENFIL
 *   CALL CHKDIRWRITE
 *   XOR AL,AL  → return 0
 * ----------------------------------------------------------------------- */
byte fn_rename(byte *fcb)
{
    byte *bp = NULL;
    byte *bx = NULL, *si = NULL;
    byte *new_name_src;

    /* MOVNAME: parse old name into NAME1 */
    if (movname(fcb, &bp) != 0)
        return 0xFF;

    /* Check for device */
    /* (MOVNAME doesn't do device check; FINDNAME will.  The ASM checks
     * BH after MOVNAME — but BH is only set by the IONAME scan that
     * runs inside FINDNAME.  We replicate the check after findname.)  */

    /* New name is at FCB offset 1 + 11 + 5 = 17 */
    new_name_src = fcb + 17;
    lodname(new_name_src, dos->NAME2);

    /* Find first match of old name */
    if (findname(&bx, &si, &bp) != 0)
        return 0xFF;

    /* Check for device */
    if (bx[1] == 0xFF)
        return 0xFF;

    /* RENFIL loop */
    do {
        int j;
        dos->DIRTYDIR = 0xFF;
        /* Copy NAME2 into directory entry, skipping '?' positions */
        for (j = 0; j < 11; j++) {
            if (dos->NAME2[j] != '?')
                bx[j] = dos->NAME2[j];
        }
    } while (contsrch(&bx, &si, bp) == 0);

    chkdirwrite(bp);
    return 0;
}
