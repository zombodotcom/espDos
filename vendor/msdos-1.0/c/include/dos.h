/*
 * dos.h — Public DOS kernel API: function numbers, dos_state, and
 *          prototypes for every subsystem.
 *
 * ASM source: 86DOS.asm (dispatch table lines 302-346, data area ~3206-3270)
 *
 * This header is the single include needed by code that calls into the
 * kernel.  It also defines the global dos_state_t structure that holds
 * all the variables that live in the CS segment in the original ASM.
 */

#ifndef DOS_H
#define DOS_H

#include "dos_types.h"
#include "fcb.h"
#include "dpb.h"
#include "bios.h"

/* -----------------------------------------------------------------------
 * System-call function numbers  (86DOS.asm DISPATCH table, lines 302-346)
 * ----------------------------------------------------------------------- */
#define FN_ABORT        0
#define FN_CONIN        1
#define FN_CONOUT       2
#define FN_READER       3
#define FN_PUNCH        4
#define FN_LIST         5
#define FN_RAWIO        6
#define FN_RAWINP       7
#define FN_IN           8
#define FN_PRTBUF       9
#define FN_BUFIN        10
#define FN_CONSTAT      11
#define FN_VERSION      12
#define FN_DSKRESET     13
#define FN_SELDSK       14
#define FN_OPEN         15
#define FN_CLOSE        16
#define FN_SRCHFRST     17
#define FN_SRCHNXT      18
#define FN_DELETE       19
#define FN_SEQRD        20
#define FN_SEQWRT       21
#define FN_CREATE       22
#define FN_RENAME       23
#define FN_INUSE        24
#define FN_CURDRV       25
#define FN_SETDMA       26
#define FN_GETFATPT     27
#define FN_WRTPROT      28
#define FN_GETRDONLY    29
#define FN_SETATTRIB    30
#define FN_GETDSKPT     31
#define FN_USERCODE     32
#define FN_RNDRD        33
#define FN_RNDWRT       34
#define FN_FILESIZE     35
#define FN_SETRNDREC    36
#define FN_SETVECT      37
#define FN_NEWBASE      38
#define FN_BLKRD        39
#define FN_BLKWRT       40
#define FN_MAKEFCB      41

/* -----------------------------------------------------------------------
 * Maximum drive count  (DRVTAB holds 15 entries, 86DOS.asm line 3231)
 * ----------------------------------------------------------------------- */
#define MAX_DRIVES      15

/*
 * Size of the internal line-input buffer (INBUF + CONBUF area).
 * ASM data area lines 3232-3239: INBUF DS 15 followed by DS 128-15
 * and CONBUF DS 130.  Total contiguous = 128 + 130 = 258 bytes but
 * INBUF is 128 bytes and CONBUF is 130 bytes (overlapping init code).
 */
#define INBUF_SIZE      128
#define CONBUF_SIZE     130

/* -----------------------------------------------------------------------
 * DOS global state
 *
 * In the original ASM all kernel variables live in the CS segment (data
 * and code share the same segment).  In the C translation they are
 * collected into this struct; a pointer  dos  is exported so all source
 * files can access them via  dos->varname .
 *
 * Variable names match the ASM labels exactly (upper-case) so a reader
 * can grep for them in 86DOS.asm.
 *
 * ASM data area: lines 3206-3268.
 * ----------------------------------------------------------------------- */
typedef struct dos_state {

    /* -- Console state -------------------------------------------------- */
    byte  CARPOS;       /* 3214: current cursor column position             */
    byte  STARTPOS;     /* 3215: column at start of current input line      */
    byte  PFLAG;        /* 3216: 1 = echo console output to printer         */

    /* -- Directory dirty flag ------------------------------------------- */
    byte  DIRTYDIR;     /* 3217: non-zero if directory buffer is dirty      */

    /* -- Drive / disk state --------------------------------------------- */
    byte  NUMDRV;       /* 3218: number of logical drives                   */
    word  CONTPOS;      /* 3219: continuation position in CONBUF            */
    word   DMAADD;      /* 3220: 16-bit offset within DMA buffer (kernel arithmetic) */
    word   DMASEG;      /* 3222: legacy; unused on host                     */
    byte  *DMABASE;     /* host pointer to user's DMA buffer (added for host) */
    word  ENDMEM;       /* 3222: first unavailable segment                  */
    word  MAXSEC;       /* 3223: largest sector size seen during init       */
    byte *BUFFER;       /* 3224: pointer to sector buffer                   */
    word  BUFSECNO;     /* 3225: sector number currently in buffer          */
    byte  BUFDRVNO;     /* 3226: drive number of buffered sector (-1=none)  */
    byte  DIRTYBUF;     /* 3227: sector buffer is dirty                     */
    word  DIRBUFID;     /* 3228: ID of directory sector in DIRBUF (-1=none) */
    word  DATE;         /* 3229: current date (packed: yr|mo|day)           */

    /* -- Drive tables --------------------------------------------------- */
    byte *CURDRVPT;     /* 3230: pointer to current drive's DPB             */
    byte *DRVTAB[MAX_DRIVES]; /* 3231: table of DPB pointers (one per drive)*/

    /* -- Currently-executing function ----------------------------------- */
    byte  FUNC;         /* 3240: function number currently being executed   */

    /* -- Directory search state ----------------------------------------- */
    word  LASTENT;      /* 3241: last directory entry number searched        */

    /* -- Exit / abort addresses ----------------------------------------- */
    word  EXITHOLD[2];  /* 3242: saved exit address (seg:off)               */
    word  FATBASE;      /* 3243: (scratch during init)                      */

    /* -- Filename buffers ----------------------------------------------- */
    byte  NAME1[11];    /* 3244: parsed file name 1                         */
    byte  NAME2[11];    /* 3245: parsed file name 2                         */

    /* -- Stack / segment save ------------------------------------------- */
    word  TEMP;         /* 3246: scratch word (also CSLOC — see below)      */
    word  CSLOC;        /* 3247: caller's CS value, saved on each syscall   */
    word  SPSAVE;       /* 3248: saved user SP                              */
    word  SSSAVE;       /* 3249: saved user SS                              */

    /* -- I/O transfer state --------------------------------------------- */
    byte  SECCLUSPOS;   /* 3250: sector position within current cluster     */
    byte  DSKERR;       /* 3251: disk error code for current transfer       */
    byte  TRANS;        /* 3252: non-zero if a transfer has occurred        */

    /* -- Record I/O work area ------------------------------------------- */
    byte *FCB_PTR;      /* 3255: pointer to user FCB for current transfer   */
    word  NEXTADD;      /* 3256: next address within DMA segment            */
    dword RECPOS;       /* 3257: record position in file (32-bit)           */
    word  RECCNT;       /* 3258: record count for current operation         */
    word  LASTPOS;      /* 3259: position of last cluster in chain          */
    word  CLUSNUM;      /* 3260: current cluster number                     */
    word  SECPOS;       /* 3261: position of first sector accessed          */
    word  VALSEC;       /* 3262: number of valid (written) sectors in file  */
    word  BYTSECPOS;    /* 3263: byte offset within first sector            */
    dword BYTPOS;       /* 3264: byte position in file (32-bit)             */
    word  BYTCNT1;      /* 3265: bytes in first (partial) sector            */
    word  BYTCNT2;      /* 3266: bytes in last  (partial) sector            */
    word  SECCNT;       /* 3267: number of whole sectors to transfer        */

    /* -- Line-input buffers --------------------------------------------- */
    byte  INBUF[INBUF_SIZE];    /* internal line-edit buffer (INBUF label)  */
    byte  CONBUF[CONBUF_SIZE];  /* console input buffer (CONBUF label)      */

    /* -- Directory sector buffer ---------------------------------------- */
    byte *DIRBUF;       /* pointer to directory sector buffer               */

    /* -- Saved-register frame (relative to user stack on syscall entry) - */
    /* Used for GETFATPT / GETDSKPT to patch return values in the frame.   */
    word  SPSAVE_VAL;   /* actual saved SP value (for pointer arithmetic)   */

} dos_state_t;

/* Global kernel state — defined in init.c */
extern dos_state_t *dos;

/* -----------------------------------------------------------------------
 * User-register save frame layout on the kernel stack
 * (86DOS.asm lines 146-160: ORG 0 / DS sequence for AXSAVE…FSAVE)
 *
 * At SAVREGS the CPU stack looks like this (offsets from saved SP = BP):
 *   [BP+0]  AX
 *   [BP+2]  BX
 *   [BP+4]  CX
 *   [BP+6]  DX
 *   [BP+8]  SI
 *   [BP+10] DI
 *   [BP+12] BP (user)
 *   [BP+14] DS
 *   [BP+16] ES
 *   [BP+18] IP
 *   [BP+20] CS
 *   [BP+22] FLAGS
 * ----------------------------------------------------------------------- */
#define AXSAVE  0
#define BXSAVE  2
#define CXSAVE  4
#define DXSAVE  6
#define SISAVE  8
#define DISAVE  10
#define BPSAVE  12
#define DSSAVE  14
#define ESSAVE  16
#define IPSAVE  18
#define CSSAVE  20
#define FSAVE   22

/* -----------------------------------------------------------------------
 * Subsystem function prototypes
 * ----------------------------------------------------------------------- */

/* fat.c */
word  fat_unpack(byte *si, word bx, byte *bp);
void  fat_pack(byte *si, word bx, word dx);
void  fat_read(byte *bp);
int   fat_write(byte *bp);
int   fat_check_write(byte *bp);
word  fat_geteof(byte *si, word bx, byte *bp);
int   fat_release(byte *si, word bx, byte *bp);
int   fat_relblks(byte *si, word bx, word dx, byte *bp);
int   fat_allocate(byte *si, word bx, word cx, word dx, byte *bp,
                   byte *fcb, word *bx_out, word *cx_out);
void  fat_write_all(void);
void  fat_figfat(byte *bp, byte *al_out, byte **bx_out, word *cx_out, word *dx_out);
word  fat_fndclus(byte *fcb, byte *si, word cx, byte *bp, word *bx_out, word *dx_out);
word  fat_figrec(word dx, byte bl, byte *bp);

/* disk.c */
int   dread(byte *buf, word count, word sector, byte *bp);
int   dwrite(byte *buf, word count, word sector, byte *bp);
void  chkdirwrite(byte *bp);
void  dirwrite(byte al, byte *bp);
void  dirread(byte al, byte *bp);

/* directory.c */
int   getfile(byte *ds_dx, byte **bx_out, byte **si_out, byte **bp_out);
int   getentry(byte *bp, byte **bx_out, byte *al_out);
int   nextentry(byte *bp, byte **bx_out, byte *dx_limit, byte *al_io);
void  startsrch(byte *bp);
int   movname(byte *fcb_ptr, byte **bp_out);
void  lodname(byte *src, byte *dst);
byte *getbp(byte drive);
int   findname(byte **bx_out, byte **si_out, byte **bp_out);
int   contsrch(byte **bx_out, byte **si_out, byte *bp);

/* file.c */
byte  fn_open(byte *ds_fcb);
byte  fn_close(byte *ds_fcb);
byte  fn_create(byte *ds_fcb);
byte  fn_delete(byte *ds_fcb);
byte  fn_rename(byte *ds_fcb);

/* io.c */
byte  fn_seqrd(byte *ds_fcb);
byte  fn_seqwrt(byte *ds_fcb);
byte  fn_rndrd(byte *ds_fcb);
byte  fn_rndwrt(byte *ds_fcb);
byte  fn_blkrd(byte *ds_fcb, word cx, word *cx_out);
byte  fn_blkwrt(byte *ds_fcb, word cx, word *cx_out);
byte  fn_setdma(byte *ds, word dx);
void  fn_filesize(byte *ds_fcb);
void  fn_setrndrec(byte *ds_fcb);
word  fn_getrec(byte *fcb, word *dx_out);
void  io_load(byte *fcb, byte *bp, byte *si);
void  io_store(byte *fcb, byte *bp, byte *si);

/* console.c */
byte  fn_conin(void);
void  fn_conout(byte ch);
byte  fn_constat(void);
byte  fn_rawio(byte dl);
byte  fn_rawinp(void);
void  fn_list(byte ch);
byte  fn_in(void);
void  fn_prtbuf(byte *ds_dx);
void  fn_bufin(byte *ds_dx);
void  con_out(byte ch);
void  con_crlf(void);
void  con_outmes(const byte *msg);

/* fcb_util.c */
byte  fn_makefcb(byte *si, byte *es_di, byte al);
byte  fn_srchfrst(byte *ds_fcb);
byte  fn_srchnxt(byte *ds_fcb);

/* syscall.c */
void  dos_dispatch(byte func, byte *ds_dx, word ax, word bx, word cx);
byte  dos_dispatch_call(byte func, byte *ds_dx, word ax, word bx, word cx);
void  fn_abort(void);
byte  fn_version(void);
byte  fn_dskreset(void);
byte  fn_seldsk(byte dl);
byte  fn_curdrv(void);
byte  fn_getfatpt(void);
byte  fn_getdskpt(void);
byte  fn_inuse(void);
byte  fn_setvect(byte al, byte *ds_dx);
void  fn_newbase(word dx_seg);

/* Return-value patch area (GETFATPT / GETDSKPT / BLKRD / BLKWRT) */
extern word  dos_ret_bx;
extern word  dos_ret_cx;
extern word  dos_ret_dx;
extern word  dos_ret_ds;

/* init.c */
void  dos_init(bios_vtable_t *bios_table, byte *init_table);

/* harderr (disk.c) */
int   harderr(const byte *msg, byte drive, byte *buf, word *cx, word *dx);

#endif /* DOS_H */
