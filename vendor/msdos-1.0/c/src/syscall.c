/*
 * syscall.c -- System call dispatcher and kernel-level management functions.
 *
 * ASM labels covered:
 *   COMMAND   86DOS.asm:199-204
 *   ENTRY     86DOS.asm:206-217
 *   SAVREGS   86DOS.asm:219-300  (register save/restore; modelled as C ABI)
 *   DISPATCH  86DOS.asm:302-346
 *   VERSION / GETIO / SETIO / WRTPROT / GETRDONLY / SETATTRIB / USERCODE
 *             86DOS.asm:348-356
 *   READER    86DOS.asm:359-361
 *   PUNCH     86DOS.asm:363-366
 *   ABORT     86DOS.asm:1217-1227
 *   ERROR     86DOS.asm:1228-1253
 *   DSKRESET  86DOS.asm:2482-2489
 *   WRTFATS   86DOS.asm:2490-2517
 *   CURDRV    86DOS.asm:2518-2524
 *   INUSE     86DOS.asm:2525-2544
 *   SELDSK    86DOS.asm:2552-2565
 *   SETDMA    86DOS.asm:2444-2449  (see also io.c fn_setdma)
 *   GETFATPT  86DOS.asm:2452-2469
 *   GETDSKPT  86DOS.asm:2472-2479
 *   SETVECT   86DOS.asm:3116-3126
 *   NEWBASE   86DOS.asm:3129-3137
 *   SETMEM    86DOS.asm:3139-3185
 *
 * Design notes:
 *   The original COMMAND/ENTRY/SAVREGS mechanism is pure 8086 interrupt
 *   machinery: the CPU pushed FLAGS/CS/IP on INT 21H, and ENTRY also handles
 *   the CP/M-compatible CALL 5 entry point by re-ordering the stack to look
 *   like an INT frame.  In the C translation this entire mechanism collapses
 *   to a simple function call: dos_dispatch(func, ds_dx, ax, bx, cx).
 *
 *   ABORT / ERROR perform far jumps through the IVT (interrupt vectors 22H
 *   and 23H) which have no direct C equivalent.  They are modelled here as
 *   calls through dos->EXITHOLD, preceded by the same FAT-flush that the
 *   ASM performs.
 *
 *   GETFATPT and GETDSKPT patch saved register values on the user stack in
 *   the ASM; in C we cannot do this directly.  We pass back the values via
 *   the global dos_ret_* variables which the dispatcher can forward.
 */

#include <stddef.h>
#include <string.h>

#include "dos.h"

/* ----------------------------------------------------------------------- */
/* Globals defined here; declared extern in dos.h / bios.h                 */
/* ----------------------------------------------------------------------- */
dos_state_t   *dos;
bios_vtable_t *bios;

/*
 * Return-value patch area.
 * GETFATPT / GETDSKPT need to write BX, CX, DX, DS into the caller's
 * register frame.  The C dispatcher exposes them here.
 * ASM: LDS SI,[SPSAVE] ; MOV [SI+BXSAVE],BX ... (lines 2464-2468)
 *
 * NOTE: differs from ASM because C has no concept of a saved register
 * frame on the stack; caller must read dos_ret_* after dispatch.
 */
word  dos_ret_bx;
word  dos_ret_cx;
word  dos_ret_dx;
word  dos_ret_ds;

/* ----------------------------------------------------------------------- */
/* Forward declaration                                                       */
/* ----------------------------------------------------------------------- */
static void do_exit_jump(void);

/* =======================================================================
 * VERSION / GETIO / SETIO / WRTPROT / GETRDONLY / SETATTRIB / USERCODE
 * ASM: 86DOS.asm:348-356
 *
 * Stub functions that return 0 in AL.  Placeholders for features not yet
 * implemented in 86-DOS 1.0.
 * ======================================================================= */

/* ASM: VERSION  86DOS.asm:348 */
byte fn_version(void)
{
    return 0;
}

/* =======================================================================
 * READER -- System call 3: auxiliary/serial input
 * ASM: READER  86DOS.asm:359-361
 *
 * Inputs:  none (BIOS AUX port)
 * Outputs: AL = character read
 * ======================================================================= */
static byte fn_reader(void)
{
    /* ASM: CALL BIOSAUXIN,BIOSSEG */
    return BIOSAUXIN();
}

/* =======================================================================
 * PUNCH -- System call 4: auxiliary/serial output
 * ASM: PUNCH  86DOS.asm:363-366
 *
 * Inputs:  DL = character to output
 * Outputs: none
 * ======================================================================= */
static void fn_punch(byte dl)
{
    /* ASM: MOV AL,DL ; CALL BIOSAUXOUT,BIOSSEG */
    BIOSAUXOUT(dl);
}

/* =======================================================================
 * ABORT -- System call 0: terminate program
 * ASM: ABORT  86DOS.asm:1217-1227
 *
 * Inputs:  CS = caller segment (CSLOC in dos state)
 * Outputs: jumps through INT 22H exit vector -- does not return
 *
 * The ASM copies the exit vector from INT 22H (address 0000:0088) into
 * the caller's PSP at offset SAVEXIT (0x000A).  In the C translation
 * we model this as a call to do_exit_jump().
 *
 * NOTE: differs from ASM because actual segment:offset manipulation and
 * real-mode IVT (0000:0088) access are not portable in C.
 * ======================================================================= */

/* ASM: ABORT  86DOS.asm:1217 */
void fn_abort(void)
{
    /* ASM lines 1218-1227:
     *   MOV DS,[CSLOC]        ; point to caller's segment
     *   XOR AX,AX
     *   MOV ES,AX             ; ES = segment 0 (IVT)
     *   MOV SI,SAVEXIT        ; SI -> INT 22H vector in IVT (0:88h)
     *   MOV DI,EXIT           ; DI -> exit vector slot in PSP (offset 0Ah)
     *   MOVW / MOVW / MOVW / MOVW   ; copy 4 words (2 far pointers)
     * then falls through to ERROR.
     */
    /* NOTE: differs from ASM because IVT access is simulated;
     * the copy of the exit vector into the caller PSP is omitted.
     * The important behaviour (flush FATs then return to caller) is
     * preserved in do_exit_jump(). */
    do_exit_jump();
}

/* =======================================================================
 * ERROR -- common exit path used by ABORT and fatal disk errors
 * ASM: ERROR  86DOS.asm:1228-1253
 *
 * Inputs:  (none -- uses dos global state)
 * Outputs: does not return; jumps through EXITHOLD to user exit handler
 *
 * Flushes all dirty FATs, restores the user SS:SP from SSSAVE/SPSAVE,
 * pops all saved registers, and performs an indirect far-jump through
 * EXITHOLD (which was loaded from INT 22H vector in the PSP area).
 * ======================================================================= */

/*
 * do_exit_jump -- C approximation of the ERROR path.
 * ASM: ERROR  86DOS.asm:1228
 *
 * NOTE: differs from ASM because the C runtime does not support far jumps
 * or stack manipulation of saved registers.  We call fat_write_all()
 * (= WRTFATS), then return.  In a full port that runs on bare metal,
 * this would need architecture-specific code to restore SS:SP and jump
 * through [EXITHOLD].
 */
static void do_exit_jump(void)
{
    /* ASM 1232: CALL WRTFATS -- flush all dirty FATs */
    fat_write_all();

    /* ASM 1234-1253: restore SS:SP, pop registers, far-jump through
     * [EXITHOLD].  In the C model we simply return; the caller (test
     * harness or init stub) must check the exit condition. */
}

/* fat_write_all() lives in fat.c; the duplicate previously defined here
 * was identical and caused a link conflict when the kernel sources are
 * compiled directly into one binary. */

/* =======================================================================
 * DSKRESET -- System call 13: reset disk system
 * ASM: DSKRESET  86DOS.asm:2482-2489-2489
 *
 * Inputs:  DS = caller segment (for DMA reset)
 * Outputs: AL = 0 (implicit)
 *
 * Resets the DMA address to the default (0x80 in the current segment),
 * resets CURDRVPT to the first drive, then flushes all dirty FATs.
 * ======================================================================= */

/* ASM: DSKRESET  86DOS.asm:2482 */
byte fn_dskreset(void)
{
    /* ASM 2484-2489:
     *   MOV [DMAADD+2],DS   ; keep DMA in current user segment
     *   MOV DS,CS           ; DS = DOS segment
     *   MOV [DMAADD],80H    ; default DMA offset = 0x80
     *   MOV AX,[CURDRVPT+2] ; CURDRVPT+2 = DRVTAB[0]
     *   MOV [CURDRVPT],AX   ; reset current drive to drive 0
     */
    dos->DMAADD   = 0x0080;   /* default DMA offset */
    /* DMASEG unchanged -- in C we have no DS register to save */
    dos->CURDRVPT = dos->DRVTAB[0];

    /* falls through to WRTFATS */
    fat_write_all();
    return 0;
}

/* =======================================================================
 * CURDRV -- System call 25: return current drive number
 * ASM: CURDRV  86DOS.asm:2518-2524-2522
 *
 * Inputs:  none
 * Outputs: AL = current drive number (0=A, 1=B, ...)
 * ======================================================================= */

/* ASM: CURDRV  86DOS.asm:2518 */
byte fn_curdrv(void)
{
    /* ASM: MOV BP,[CURDRVPT] ; MOV AL,[BP+DRVNUM] */
    return DPB_GET_BYTE(dos->CURDRVPT, DRVNUM);
}

/* =======================================================================
 * INUSE -- System call 24: return bitmap of drives with dirty FATs
 * ASM: INUSE  86DOS.asm:2525-2544-2542
 *
 * Inputs:  none
 * Outputs: AL = bitmap; bit N set if drive N's FAT has unsaved changes.
 *   DIRTYFAT == 0xFF (-1) means "never been read" and is treated as NOT
 *   in-use.  The ASM check is: CMP B,[BP+DIRTYFAT],-1 ; RCL BX which
 *   sets carry when the byte != 0xFF (i.e., the FAT has been touched).
 *
 * NOTE: differs from ASM because the ASM iterates backwards using the
 * DOWN direction and RCL BX to shift carry bits into the result.  The C
 * version builds the bitmap with explicit shifts; the bit order is the
 * same (drive 0 in bit 0 of the result).
 * ======================================================================= */

/* ASM: INUSE  86DOS.asm:2525 */
byte fn_inuse(void)
{
    int i;
    word bx = 0;
    int numdrv = dos->NUMDRV;

    /* ASM iterates from last entry down to first.  RCL BX after each
     * iteration means the first drive ends up in bit 0. */
    for (i = numdrv - 1; i >= 0; i--) {
        byte *bp = dos->DRVTAB[i];
        int dirty = (bp != NULL) &&
                    (DPB_GET_BYTE(bp, DIRTYFAT) != DIRTYFAT_UNREAD);
        bx = (bx << 1) | (dirty ? 1 : 0);
    }
    return (byte)bx;
}

/* =======================================================================
 * SELDSK -- System call 14: select default drive
 * ASM: SELDSK  86DOS.asm:2552-2565-2563
 *
 * Inputs:  DL = drive number (0=A, 1=B, ...)
 * Outputs: none (CURDRVPT updated); request silently ignored if DL >= NUMDRV
 * ======================================================================= */

/* ASM: SELDSK  86DOS.asm:2552 */
byte fn_seldsk(byte dl)
{
    /* ASM:
     *   MOV DH,0             ; zero-extend DL into DX
     *   MOV BX,DX
     *   MOV AL,[NUMDRV]
     *   CMP BL,AL
     *   JNB RET              ; drive >= NUMDRV: ignore
     *   SHL BX               ; BX = drive * 2 (word index)
     *   MOV DX,[BX+CURDRVPT+2]  ; DX = DRVTAB[drive]
     *   MOV [CURDRVPT],DX    ; update CURDRVPT
     */
    if (dl < dos->NUMDRV) {
        dos->CURDRVPT = dos->DRVTAB[dl];
    }
    return 0;
}

/* =======================================================================
 * GETFATPT -- System call 27: get FAT pointer for current drive
 * ASM: GETFATPT  86DOS.asm:2452-2469
 *
 * Inputs:  none
 * Outputs (placed in saved register frame in original ASM):
 *   BX = pointer to FAT in DOS segment
 *   AL = sectors/cluster  (CLUSMSK+1)
 *   DX = MAXCLUS - 1
 *   CX = sector size (SECSIZ)
 *   DS = DOS segment (CS in original)
 *   Side effect: DIRTYFAT set to 1 (dirty)
 *
 * NOTE: differs from ASM because the ASM patches the caller's saved
 * register frame directly via [SPSAVE].  In C the values are returned
 * through the dos_ret_* globals so the dispatcher can forward them.
 * ======================================================================= */

/* ASM: GETFATPT  86DOS.asm:2452 */
byte fn_getfatpt(void)
{
    byte *bp = dos->CURDRVPT;

    /* ASM 2456: CALL FATREAD -- ensure FAT is in memory */
    fat_read(bp);

    /* ASM 2457-2468: compute and patch return registers.
     * NOTE: BX in ASM is a 16-bit offset within the DOS CS segment; here
     * we store the pointer value cast to word. */
    dos_ret_bx = (word)(uintptr_t)DPB_FAT_PTR(bp);
    dos_ret_dx = DPB_GET_WORD(bp, MAXCLUS) - 1;
    dos_ret_cx = DPB_GET_WORD(bp, SECSIZ);

    /* Mark FAT dirty (caller may modify it) */
    DPB_SET_BYTE(bp, DIRTYFAT, DIRTYFAT_DIRTY);

    /* AL = CLUSMSK + 1 = sectors per cluster */
    return (byte)(DPB_GET_BYTE(bp, CLUSMSK) + 1);
}

/* =======================================================================
 * GETDSKPT -- System call 31: get Drive Parameter Block pointer
 * ASM: GETDSKPT  86DOS.asm:2472-2479
 *
 * Inputs:  none
 * Outputs (placed in saved register frame in original ASM):
 *   BX = pointer to current drive's DPB
 *   DS = DOS segment (CS in original)
 *
 * NOTE: differs from ASM because same patched-register-frame issue as
 * GETFATPT; values returned via dos_ret_* globals.
 * ======================================================================= */

/* ASM: GETDSKPT  86DOS.asm:2472 */
byte fn_getdskpt(void)
{
    /* ASM:
     *   MOV BX,[CURDRVPT]
     *   LDS SI,[SPSAVE]
     *   MOV [SI+BXSAVE],BX
     *   MOV [SI+DSSAVE],CS
     */
    dos_ret_bx = (word)(uintptr_t)dos->CURDRVPT;
    /* dos_ret_ds would be the DOS segment -- not meaningful in flat C */
    return 0;
}

/* =======================================================================
 * SETVECT -- System call 37: set interrupt vector
 * ASM: SETVECT  86DOS.asm:3116-3126
 *
 * Inputs:
 *   AL = interrupt number
 *   DS:DX = new handler address
 * Outputs: none
 *
 * Writes the far pointer DS:DX into the real-mode IVT at 0000:(AL*4).
 *
 * NOTE: differs from ASM because real-mode IVT access is not portable in
 * C.  A bare-metal port would write to physical address (al * 4).  This
 * implementation is a no-op stub.
 * ======================================================================= */

/* ASM: SETVECT  86DOS.asm:3116 */
byte fn_setvect(byte al, byte *ds_dx)
{
    /* ASM:
     *   XOR BX,BX
     *   MOV ES,BX         ; ES = 0 (IVT segment)
     *   MOV BL,AL
     *   SHL BX / SHL BX   ; BX = AL * 4
     *   MOV ES:[BX],DX    ; offset
     *   MOV ES:[BX+2],DS  ; segment
     *
     * NOTE: no real-mode IVT in portable C.  Bare-metal port:
     *   word *ivt = (word *)0;
     *   ivt[al * 2]     = offset_of(ds_dx);
     *   ivt[al * 2 + 1] = segment_of(ds_dx);
     */
    (void)al;
    (void)ds_dx;
    return 0;
}

/* =======================================================================
 * fn_setmem -- SETMEM helper, also used standalone
 * ASM: SETMEM  86DOS.asm:3139-3185
 *
 * Inputs:  dx_seg = segment of new program base; base_ptr = pointer to it
 * Outputs: Initialises the program base:
 *   [0]    = INT 20H  (0x20CD)
 *   [2]    = ENDMEM   (first unavailable segment)
 *   [5]    = 0x9A     (far call opcode = LONGCALL)
 *   [6:7]  = entry point offset (CALL 5 target)
 *   [8:9]  = entry point segment
 *   [0Ah-0Fh] = INT 22H / 23H / 24H vectors copied from IVT
 *
 * NOTE: differs from ASM because segment arithmetic, IVT access, and the
 * ENTRYPOINTSEG / MAXDIF calculation (lines 3166-3184) are real-mode
 * specific.  In the C model these are stubbed.
 * ======================================================================= */
static void fn_setmem(word dx_seg, byte *base_ptr)
{
    /* ASM 3156-3185 (abbreviated):
     *   XOR CX,CX
     *   MOV DS,CX              ; DS = 0 (IVT)
     *   MOV ES,DX              ; ES = new base
     *   ; copy INT 22H/23H/24H vectors from IVT to PSP[0Ah..0Fh]
     *   MOV [ES:2], ENDMEM
     *   MOV [ES:0], 20CDH      ; INT 20H
     *   MOV B[ES:5], LONGCALL  ; far-call opcode
     */

    /* Write INT 20H at PSP:0000 */
    base_ptr[0] = 0xCD;   /* INT */
    base_ptr[1] = 0x20;   /* 20H */

    /* Write ENDMEM at PSP:0002 */
    *(word *)(base_ptr + 2) = dos->ENDMEM;

    /* Write far-call opcode (LONGCALL = 0x9A) at PSP:0005 */
    base_ptr[5] = 0x9A;

    /* Entry-point far address at PSP:0006 and PSP:0008.
     * NOTE: differs from ASM because the real calculation depends on
     * runtime segment values.  We write zeros as placeholders. */
    *(word *)(base_ptr + 6) = 0x0000;
    *(word *)(base_ptr + 8) = dx_seg;
}

/* =======================================================================
 * NEWBASE -- System call 38: copy PSP and set new program base segment
 * ASM: NEWBASE  86DOS.asm:3129-3137
 *
 * Inputs:  DX = new base segment
 * Outputs: none  (falls through to SETMEM)
 *
 * Copies 256 bytes (0x80 words) from the current caller segment (CSLOC)
 * to the new segment DX, then calls fn_setmem() to initialise the program
 * base area.
 *
 * NOTE: differs from ASM because segment-to-segment copy requires a
 * real-mode pointer.  In a bare-metal port CSLOC would be the physical
 * caller segment address.
 * ======================================================================= */

/* ASM: NEWBASE  86DOS.asm:3129 */
void fn_newbase(word dx_seg)
{
    /* ASM 3130-3137:
     *   MOV ES,DX              ; ES = new base segment
     *   MOV DS,[CSLOC]         ; DS = caller's segment
     *   XOR SI,SI
     *   MOV DI,SI
     *   MOV CX,80H             ; 128 words = 256 bytes
     *   REP MOVW               ; copy 256 bytes from DS:0 to ES:0
     * then falls through to SETMEM.
     */
    (void)dx_seg;
    /* fn_setmem(dx_seg, ptr_to_new_base);  -- requires real segment support */
}

/* =======================================================================
 * dos_dispatch_call -- C equivalent of COMMAND/ENTRY/SAVREGS/DISPATCH
 * ASM: COMMAND  86DOS.asm:199-204
 *      ENTRY    86DOS.asm:206-217
 *      SAVREGS  86DOS.asm:219-300
 *      DISPATCH 86DOS.asm:302-346
 *
 * In the ASM the interrupt / CALL-5 entry points save all GP registers and
 * the flags onto the kernel stack, call the appropriate handler via the
 * DISPATCH word table, then restore all registers and IRET.  In C that
 * machinery collapses to a switch statement.
 *
 * Parameters mirror the most common subset of input registers:
 *   func   -- AH (function number); checked against MAXCOM / MAXCALL
 *   ds_dx  -- DS:DX pointer (user buffer / FCB)
 *   ax     -- full AX (AL used by RAWIO, SETVECT, MAKEFCB)
 *   bx     -- BX (second pointer for MAKEFCB)
 *   cx     -- CX (record count for BLKRD/BLKWRT)
 *
 * Returns: AL result byte (placed back in user AX in the original).
 * ======================================================================= */
byte dos_dispatch_call(byte func, byte *ds_dx, word ax, word bx, word cx)
{
    byte al = (byte)ax;

    /* ASM 200-204: CMP AH,MAXCOM ; JBE SAVREGS ; MOV AL,0 ; IRET */
    if (func > MAXCOM) {
        return 0;  /* BADCALL */
    }

    switch (func) {
    /* 0  ABORT */
    case FN_ABORT:
        fn_abort();
        return 0;

    /* 1  CONIN */
    case FN_CONIN:
        return fn_conin();

    /* 2  CONOUT */
    case FN_CONOUT:
        fn_conout((byte)ax);
        return 0;

    /* 3  READER -- aux input */
    case FN_READER:
        return fn_reader();

    /* 4  PUNCH -- aux output */
    case FN_PUNCH:
        fn_punch(al);
        return 0;

    /* 5  LIST -- printer output */
    case FN_LIST:
        fn_list(al);
        return 0;

    /* 6  RAWIO */
    case FN_RAWIO:
        return fn_rawio(al);

    /* 7  RAWINP */
    case FN_RAWINP:
        return fn_rawinp();

    /* 8  IN -- console input with echo */
    case FN_IN:
        return fn_in();

    /* 9  PRTBUF -- print string */
    case FN_PRTBUF:
        fn_prtbuf(ds_dx);
        return 0;

    /* 10 BUFIN -- buffered line input */
    case FN_BUFIN:
        fn_bufin(ds_dx);
        return 0;

    /* 11 CONSTAT -- console status */
    case FN_CONSTAT:
        return fn_constat();

    /* 12 VERSION */
    case FN_VERSION:
        return fn_version();

    /* 13 DSKRESET */
    case FN_DSKRESET:
        return fn_dskreset();

    /* 14 SELDSK */
    case FN_SELDSK:
        return fn_seldsk(al);

    /* 15 OPEN */
    case FN_OPEN:
        return fn_open(ds_dx);

    /* 16 CLOSE */
    case FN_CLOSE:
        return fn_close(ds_dx);

    /* 17 SRCHFRST */
    case FN_SRCHFRST:
        return fn_srchfrst(ds_dx);

    /* 18 SRCHNXT */
    case FN_SRCHNXT:
        return fn_srchnxt(ds_dx);

    /* 19 DELETE */
    case FN_DELETE:
        return fn_delete(ds_dx);

    /* 20 SEQRD */
    case FN_SEQRD:
        return fn_seqrd(ds_dx);

    /* 21 SEQWRT */
    case FN_SEQWRT:
        return fn_seqwrt(ds_dx);

    /* 22 CREATE */
    case FN_CREATE:
        return fn_create(ds_dx);

    /* 23 RENAME */
    case FN_RENAME:
        return fn_rename(ds_dx);

    /* 24 INUSE */
    case FN_INUSE:
        return fn_inuse();

    /* 25 CURDRV */
    case FN_CURDRV:
        return fn_curdrv();

    /* 26 SETDMA */
    case FN_SETDMA:
        return fn_setdma(ds_dx, (word)(uintptr_t)ds_dx);

    /* 27 GETFATPT */
    case FN_GETFATPT:
        return fn_getfatpt();

    /* 28 WRTPROT -- stub */
    case FN_WRTPROT:
        return 0;

    /* 29 GETRDONLY -- stub */
    case FN_GETRDONLY:
        return 0;

    /* 30 SETATTRIB -- stub */
    case FN_SETATTRIB:
        return 0;

    /* 31 GETDSKPT */
    case FN_GETDSKPT:
        return fn_getdskpt();

    /* 32 USERCODE -- stub */
    case FN_USERCODE:
        return 0;

    /* 33 RNDRD */
    case FN_RNDRD:
        return fn_rndrd(ds_dx);

    /* 34 RNDWRT */
    case FN_RNDWRT:
        return fn_rndwrt(ds_dx);

    /* 35 FILESIZE */
    case FN_FILESIZE:
        fn_filesize(ds_dx);
        return 0;

    /* 36 SETRNDREC */
    case FN_SETRNDREC:
        fn_setrndrec(ds_dx);
        return 0;

    /* 37 SETVECT */
    case FN_SETVECT:
        return fn_setvect(al, ds_dx);

    /* 38 NEWBASE */
    case FN_NEWBASE:
        fn_newbase((word)(uintptr_t)ds_dx);
        return 0;

    /* 39 BLKRD */
    case FN_BLKRD: {
        word cx_out = 0;
        byte ret = fn_blkrd(ds_dx, cx, &cx_out);
        dos_ret_cx = cx_out;
        return ret;
    }

    /* 40 BLKWRT */
    case FN_BLKWRT: {
        word cx_out = 0;
        byte ret = fn_blkwrt(ds_dx, cx, &cx_out);
        dos_ret_cx = cx_out;
        return ret;
    }

    /* 41 MAKEFCB */
    case FN_MAKEFCB:
        /* NOTE: differs from ASM because in ASM ES:DI is the destination FCB
         * pointer.  Here we pass bx as the destination address cast to byte*. */
        return fn_makefcb(ds_dx, (byte *)(uintptr_t)bx, al);

    default:
        return 0;
    }
}

/*
 * dos_dispatch -- public wrapper matching the prototype in dos.h
 * ASM: see COMMAND/ENTRY/DISPATCH above
 */
void dos_dispatch(byte func, byte *ds_dx, word ax, word bx, word cx)
{
    dos->FUNC = func;
    dos_dispatch_call(func, ds_dx, ax, bx, cx);
}
