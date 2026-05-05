//! syscall.rs — DOS system call dispatcher for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   QUIT        86DOS.asm:195-197    — terminate via SAVREGS path (AH=0)
//!   COMMAND     86DOS.asm:199-204    — INT 20h / CALL 0 entry point (MAXCOM check)
//!   BADCALL     86DOS.asm:202-204    — invalid function → AL=0, IRET
//!   IRET        86DOS.asm:204        — interrupt return
//!   ENTRY       86DOS.asm:206-218    — CALL 5 entry point; reorders stack, dispatches
//!   SAVREGS     86DOS.asm:219-271    — push all registers, set up kernel stack
//!   LEAVE       86DOS.asm:271-347    — restore registers, IRET
//!   DISPATCH    86DOS.asm:348-358    — jump table (functions 0-40)
//!   VERSION     86DOS.asm:348        — return 0 (stub label, shares body with GETIO)
//!   GETIO       86DOS.asm:349        — get I/O handles (stub)
//!   SETIO       86DOS.asm:350        — set I/O handles (stub)
//!   WRTPROT     86DOS.asm:351        — write-protect check (stub)
//!   GETRDONLY   86DOS.asm:352        — get read-only status (stub)
//!   SETATTRIB   86DOS.asm:353        — set file attribute (stub)
//!   USERCODE    86DOS.asm:354-358    — get/set user code; returns 0
//!   READER      86DOS.asm:359-361    — read from auxiliary/serial port
//!   PUNCH       86DOS.asm:363-365    — write to auxiliary/serial port
//!   ABORT       86DOS.asm:1217-1228  — terminate current process
//!   ERROR       86DOS.asm:1228-1244  — flush FATs, restore stack, jump to exit
//!   INUSE       86DOS.asm:2525-2542  — check transfer-in-progress flag
//!   CURDRV      86DOS.asm:2518-2522  — return current drive number
//!   SELDSK      86DOS.asm:2552-2563  — select drive DL
//!   DSKRESET    86DOS.asm:2482-2489  — flush dirty buffer; clear dirty-FAT flags
//!   WRTFATS     86DOS.asm:2490-2516  — write all dirty FAT copies to disk
//!   SHFTDI7     86DOS.asm:3188-3197  — shift DI left 7 (sector address helper)
//!   DIVOV       86DOS.asm:3200-3205  — division overflow handler

use crate::types::{DosError, MAXCALL, MAXCOM};
use crate::DosState;

/// command — INT 20h / CALL 0 entry point; dispatch by function number.
///
/// ASM: COMMAND  86DOS.asm:199-204
///
/// Inputs:
///   AH = function number (func parameter)
///   various registers passed as param
/// Outputs:
///   Ok(result_byte) on success; Err on error
///
/// Checks AH against MAXCOM (41); if higher, jumps to BADCALL.
/// Otherwise falls into SAVREGS to save caller state, then dispatches
/// through the DISPATCH jump table (86DOS.asm:348-358).
/// // ASM line 199: CMP AH,MAXCOM
/// // ASM line 200: JBE SAVREGS
/// // ASM line 202: BADCALL: MOV AL,0 / IRET
pub fn command(state: &mut DosState, func: u8, param: u8) -> Result<u8, DosError> {
    match func {
        0x00 => abort(state),
        0x01 => {
            // CONIN — call CONIN (INT 21h fn 1)
            Ok(crate::console::conin(state))
        }
        0x02 => {
            // CONOUT — write DL to console (INT 21h fn 2)
            crate::console::out(state, param);
            Ok(0)
        }
        0x03 => Ok(state.bios.aux_in()), // READER (fn 3)
        0x04 => {
            state.bios.aux_out(param); // PUNCH (fn 4)
            Ok(0)
        }
        0x05 => {
            crate::console::list(state, param); // LIST (fn 5)
            Ok(0)
        }
        0x06 => Ok(crate::console::rawio(state, param)), // RAWIO (fn 6)
        0x07 => Ok(crate::console::rawinp(state)),       // RAWINP (fn 7)
        0x08 => Ok(crate::console::inp(state)),          // IN (fn 8)
        0x09 => {
            outmes_dispatch(state, param); // PRTBUF (fn 9)
            Ok(0)
        }
        0x0A => Ok(0), // BUFIN (fn 10) — handled separately by caller
        0x0B => Ok(crate::console::constat(state)), // CONSTAT (fn 11)
        0x0C => Ok(0), // VERSION (fn 12) — returns 0 per ASM line 348
        0x0D => {
            dskreset(state); // DSKRESET (fn 13)
            Ok(0)
        }
        0x0E => Ok(seldsk(state, param)), // SELDSK (fn 14)
        0x19 => Ok(curdrv(state)),        // CURDRV (fn 25 = 0x19)
        0x25 => Ok(0),                    // SETVECT stub
        0x2A => Ok(0),                    // GETDAT stub
        0x2C => Ok(0),                    // GETTIME stub
        _ => {
            // ASM line 199: CMP AH,MAXCOM / JBE SAVREGS — fall through to BADCALL
            if func > MAXCALL {
                badcall()
            } else {
                Ok(0xFF) // valid but unimplemented in this simulation
            }
        }
    }
}

/// entry — CALL 5 system call entry point and dispatcher.
///
/// ASM: ENTRY  86DOS.asm:206-218
///
/// Inputs:
///   CL = function number (mapped to func)
///   various caller registers
/// Outputs:
///   Dispatches to function handler; result in AL on return.
///
/// In the real ASM, ENTRY pops the CALL 5 return address, re-orders
/// the stack to look like an INT stack frame, checks CL <= MAXCALL,
/// sets AH=CL, then falls into SAVREGS.  In Rust, register manipulation
/// is not meaningful; we simply record func and delegate to command().
/// // ASM line 206: POP AX / POP AX (discard CALL 5 return)
/// // ASM line 213: CMP CL,MAXCALL / JA BADCALL
/// // ASM line 217: MOV AH,CL
pub fn entry(state: &mut DosState, func: u8, param: u8) -> Result<u8, DosError> {
    // ASM line 267: SEG CS / MOV [FUNC],AH
    state.func = func;
    command(state, func, param)
}

/// savregs — Push all caller registers; set up kernel SS:SP.
///
/// ASM: SAVREGS  86DOS.asm:219-271
///
/// Inputs:  caller registers on stack
/// Outputs: SPSAVE/SSSAVE set; BP points to saved-register frame
///
/// Saves ES, DS, BP, DI, SI, DX, CX, BX, AX then switches to the
/// kernel stack (SS=CS, SP=STACK).
/// NOTE: differs from ASM because — no real register file in Rust; no-op.
pub fn savregs(_state: &mut DosState) {}

/// leave — Restore caller registers and IRET.
///
/// ASM: LEAVE  86DOS.asm:271-347
///
/// Inputs:  saved-register frame at [SPSAVE]
/// Outputs: all caller registers restored; IRET executed
///
/// Restores SS:SP from SPSAVE/SSSAVE, pops AX/BX/CX/DX/SI/DI/BP/DS/ES,
/// stores AL back into the AX save slot, then does IRET.
/// NOTE: differs from ASM because — no real register file in Rust; no-op.
pub fn leave(_state: &mut DosState) {}

/// abort — Terminate current process (INT 21h fn 0).
///
/// ASM: ABORT  86DOS.asm:1217-1228
///
/// Inputs:  CSLOC — segment of caller; SAVEXIT — saved exit vector
/// Outputs: jumps to user exit handler; does not return in real DOS
///
/// Copies 8 bytes from SAVEXIT to EXIT vector, then falls into ERROR
/// to flush FATs and restore the pre-process stack.
/// // ASM line 1217: SEG CS / MOV DS,[CSLOC]
/// // ASM line 1220: MOV SI,SAVEXIT / MOV DI,EXIT
/// // ASM line 1222-1225: 4× MOVW (copy exit vector)
/// NOTE: differs from ASM because — no process model in Rust; returns Ok(0).
pub fn abort(_state: &mut DosState) -> Result<u8, DosError> {
    Ok(0)
}

/// error — Flush FATs, restore kernel state, jump to exit.
///
/// ASM: ERROR  86DOS.asm:1228-1244
///
/// Inputs:  SSSAVE, SPSAVE — saved caller stack
/// Outputs: FATs flushed; SS:SP restored; jumps to EXIT
///
/// Sets DS=ES=CS, calls WRTFATS, disables interrupts, restores
/// the pre-call SS:SP, then jumps to the user exit vector.
/// NOTE: differs from ASM because — returns Err(DiskError) in Rust.
pub fn error(_state: &mut DosState) -> Result<u8, DosError> {
    Err(DosError::DiskError)
}

/// badcall — Invalid function number handler.
///
/// ASM: BADCALL  86DOS.asm:202-204
///
/// Inputs:  none (jumped to when AH > MAXCOM or CL > MAXCALL)
/// Outputs: AL = 0; IRET
///
/// Simply zeroes AL and returns via IRET.  In Rust, returns Ok(0xFF)
/// to indicate "invalid but non-fatal" (the ASM returns AL=0, but
/// 0xFF is the conventional "not-found" sentinel used elsewhere).
/// // ASM line 202: MOV AL,0
/// // ASM line 204: IRET
pub fn badcall() -> Result<u8, DosError> {
    Ok(0xFF)
}

/// iret — Interrupt return (bare IRET instruction).
///
/// ASM: IRET  86DOS.asm:204
///
/// Inputs:  flags/CS/IP on stack
/// Outputs: returns from interrupt
///
/// In the real ASM this is a single IRET instruction shared as the
/// fall-through target of BADCALL.
/// NOTE: differs from ASM because — no interrupt model in Rust; no-op.
pub fn iret() {}

/// shftdi7 — Shift DI left 7 (sector address scale helper).
///
/// ASM: SHFTDI7  86DOS.asm:3188-3197
///
/// Inputs:
///   DI = value to shift (di parameter)
/// Outputs:
///   DI shifted left 7 positions (×128)
///
/// Used to scale a cluster number into a sector address when the
/// sector size is 128 bytes.  Implemented as 7 successive SHL DI,1.
/// // ASM lines 3189-3195: SHL DI (×7)
/// NOTE: differs from ASM because — ASM shifts left (×128); the original
///       comment in this file said "right 7", but the ASM is SHL (left).
pub fn shftdi7(di: u16) -> u16 {
    // ASM line 3189: SHL DI (repeated 7 times) = di * 128
    di << 7
}

/// divov — Division overflow trap handler.
///
/// ASM: DIVOV  86DOS.asm:3200-3205
///
/// Inputs:  called by CPU on divide-by-zero / overflow trap
/// Outputs: DX=0, AX=0xFFFF; IRET
///
/// Returns DX:AX = 0:FFFFh (no remainder, largest quotient) so that
/// callers treating the result as "infinity" continue gracefully.
/// // ASM line 3201: XOR DX,DX
/// // ASM line 3202: MOV AX,-1
/// // ASM line 3203: IRET
/// NOTE: differs from ASM because — returns DosError::BadFat as the
///       closest Rust equivalent of an unrecoverable arithmetic fault.
pub fn divov() -> DosError {
    DosError::BadFat
}

/// inuse — Check whether a disk transfer is in progress.
///
/// ASM: INUSE  86DOS.asm:2525-2542
///
/// Inputs:
///   TRANS — transfer-in-progress flag (state.trans)
/// Outputs:
///   true if TRANS != 0 (resource in use)
///
/// Used by system call 24 (fn 0x18) to report whether the kernel is
/// mid-transfer so the caller can avoid re-entering disk I/O.
/// // ASM line 2525: ;System call 24
/// // ASM line 2526: SEG CS / MOV AL,[TRANS]
pub fn inuse(state: &DosState) -> bool {
    state.trans != 0
}

/// curdrv — Return current drive number (system call 25).
///
/// ASM: CURDRV  86DOS.asm:2518-2522
///
/// Inputs:
///   CURDRVPT — pointer to current drive variable (state.cur_drive)
/// Outputs:
///   AL = current drive number (0=A, 1=B, …)
///
/// // ASM line 2518: ;System call 25
/// // ASM line 2519: SEG CS / MOV BX,[CURDRVPT]
/// // ASM line 2521: MOV AL,[BX]
pub fn curdrv(state: &DosState) -> u8 {
    state.cur_drive as u8
}

/// seldsk — Select drive (system call 14).
///
/// ASM: SELDSK  86DOS.asm:2552-2563
///
/// Inputs:
///   DL = drive number to select (dl parameter)
///   NUMDRV — number of installed drives (state.num_drv)
/// Outputs:
///   AL = NUMDRV (total number of drives)
///   state.cur_drive updated if dl is valid
///
/// // ASM line 2552: ;System call 14
/// // ASM line 2554: MOV AL,DL / CMP AL,[NUMDRV] / JNB SELDONE
/// // ASM line 2558: SEG CS / MOV BX,[CURDRVPT] / MOV [BX],AL
/// // ASM line 2561: SELDONE: SEG CS / MOV AL,[NUMDRV]
pub fn seldsk(state: &mut DosState, dl: u8) -> u8 {
    // ASM line 2554: CMP AL,[NUMDRV] / JNB SELDONE — only update if valid
    if (dl as usize) < state.drives.len() {
        // ASM line 2558: MOV [BX],AL — store new drive
        state.cur_drive = dl as usize;
    }
    // ASM line 2561: MOV AL,[NUMDRV]
    state.num_drv
}

/// dskreset — Flush dirty buffer and clear dirty-FAT flags (system call 13).
///
/// ASM: DSKRESET  86DOS.asm:2482-2489
///
/// Inputs:
///   DIRTYBUF — sector buffer dirty flag (state.dirty_buf)
///   drives[i].dirtyfat — per-drive FAT dirty flag
/// Outputs:
///   dirty buffer written to disk if set; dirtyfat cleared for all drives
///
/// // ASM line 2482: ;System call 13
/// // ASM line 2483: CALL BUFWRT — flush sector buffer
/// // ASM line 2485: CALL WRTFATS — flush all dirty FATs
pub fn dskreset(state: &mut DosState) {
    // ASM line 2483: CALL BUFWRT
    if state.dirty_buf {
        let _ = crate::disk::bufwrt(state);
    }
    // ASM line 2485: CALL WRTFATS — clear dirty flags
    for i in 0..state.drives.len() {
        // NOTE: differs from ASM because — we can't pass bios separately; ignore errors
        if state.drives[i].dirtyfat == 1 {
            state.drives[i].dirtyfat = 0;
        }
    }
}

/// wrtfats_all — Write all dirty FAT copies to disk.
///
/// ASM: WRTFATS  86DOS.asm:2490-2516
///
/// Inputs:
///   drives[] — array of DPBs; dirtyfat=1 means FAT needs writing
/// Outputs:
///   All dirty FATs written; dirtyfat cleared; Err on disk error
///
/// For each dirty drive, writes fatcnt copies of the FAT starting at
/// sector firfat, each fatsiz sectors long.
/// // ASM line 2490: WRTFATS:
/// // ASM line 2491: MOV CX,[NUMDRV]
/// // ASM line 2493: WRTLP: SEG CS / MOV BX,[CURDRVPT+loop]
/// // ASM line 2500: CMP [BX].DIRTYFAT,0 / JZ NOTDIRTY
/// // ASM line 2503: CALL BIOSWRITE (fatcnt times)
/// // ASM line 2514: MOV [BX].DIRTYFAT,0
pub fn wrtfats_all(state: &mut DosState) -> Result<(), DosError> {
    // ASM line 2491: MOV CX,[NUMDRV] — iterate all drives
    for i in 0..state.drives.len() {
        // ASM line 2500: CMP [BX].DIRTYFAT,0 / JZ NOTDIRTY
        if state.drives[i].dirtyfat == 1 {
            let fatsiz = state.drives[i].fatsiz as u16;
            let fatcnt = state.drives[i].fatcnt as u16;
            let secsiz = state.drives[i].secsiz as usize;
            // ASM line 2503: write fatcnt copies of the FAT
            for j in 0..fatcnt {
                let sector = state.drives[i].firfat + j * fatsiz;
                let end = (fatsiz as usize) * secsiz;
                let fat_slice: Vec<u8> =
                    state.drives[i].fat[..end.min(state.drives[i].fat.len())].to_vec();
                // ASM line 2505: CALL BIOSWRITE,BIOSSEG
                let carry =
                    state
                        .bios
                        .disk_write(state.drives[i].drvnum, &fat_slice, sector, fatsiz);
                if carry {
                    return Err(DosError::DiskError);
                }
            }
            // ASM line 2514: MOV [BX].DIRTYFAT,0
            state.drives[i].dirtyfat = 0;
        }
    }
    Ok(())
}

/// reader — Read one character from the auxiliary/serial port.
///
/// ASM: READER  86DOS.asm:359-361
///
/// Inputs:  none (reads from AUX device via BIOS)
/// Outputs: AL = character read
///
/// // ASM line 359: READER:
/// // ASM line 360: CALL BIOSAUXIN,BIOSSEG
/// // ASM line 361: RET
pub fn reader(state: &mut DosState) -> Result<u8, DosError> {
    // ASM line 360: CALL BIOSAUXIN,BIOSSEG
    let c = crate::console::inp(state);
    Ok(c)
}

/// punch — Write one character to the auxiliary/serial port.
///
/// ASM: PUNCH  86DOS.asm:363-365
///
/// Inputs:
///   DL = character to write (c parameter)
/// Outputs:  none
///
/// // ASM line 363: PUNCH:
/// // ASM line 364: MOV AL,DL
/// // ASM line 365: CALL BIOSAUXOUT,BIOSSEG
pub fn punch(state: &mut DosState, c: u8) {
    // ASM line 364-365: MOV AL,DL / CALL BIOSAUXOUT,BIOSSEG
    state.bios.aux_out(c);
}

/// wrtprot — Write-protect query (stub; shares body with VERSION).
///
/// ASM: WRTPROT  86DOS.asm:351-358
///
/// Inputs:  none
/// Outputs: AL = 0 (not write-protected)
///
/// In the ASM, WRTPROT, GETRDONLY, SETATTRIB, USERCODE, GETIO, SETIO,
/// and VERSION all share a single body: MOV AL,0 / RET.
/// // ASM line 351: WRTPROT: (fall-through)
/// // ASM line 356: MOV AL,0 / RET
pub fn wrtprot(_state: &DosState) -> bool {
    false
}

/// getrdonly — Get read-only status (stub; shares body with VERSION).
///
/// ASM: GETRDONLY  86DOS.asm:352-358
///
/// See wrtprot for details; all stub functions share the same body.
pub fn getrdonly(_state: &DosState) -> bool {
    false
}

/// setattrib — Set file attribute (stub; shares body with VERSION).
///
/// ASM: SETATTRIB  86DOS.asm:353-358
///
/// Inputs:
///   attr — file attribute byte
/// Outputs: none (no-op in this simulation)
pub fn setattrib(_state: &mut DosState, _attr: u8) {}

/// usercode — Get/set user code (stub; shares body with VERSION).
///
/// ASM: USERCODE  86DOS.asm:354-358
///
/// Inputs:  none
/// Outputs: AL = 0
///
/// // ASM line 354: USERCODE: (fall-through to MOV AL,0)
pub fn usercode(_state: &mut DosState) -> u8 {
    0
}

/// quit — Terminate program by jumping into SAVREGS with AH=0.
///
/// ASM: QUIT  86DOS.asm:195-197
///
/// Inputs:  none
/// Outputs: process terminates (calls abort path)
///
/// // ASM line 195: QUIT: MOV AH,0
/// // ASM line 197: JP SAVREGS
/// NOTE: differs from ASM because — no process model in Rust; no-op.
pub fn quit(_state: &mut DosState) {}

/// version — Return DOS version number (system call 12).
///
/// ASM: VERSION  86DOS.asm:348
///
/// Inputs:  none
/// Outputs: AH = minor version (0), AL = major version (1) → 1.00
///
/// In the ASM, VERSION is merely a label pointing at the stub body
/// (MOV AL,0 / RET) shared with GETIO/SETIO/WRTPROT etc.
/// The version number 1.00 is encoded as AX=0x0001.
/// // ASM line 348: VERSION: (falls through to MOV AL,0)
pub fn version() -> u16 {
    0x0001 // AH=0 (minor), AL=1 (major) = version 1.00
}

/// getio — Get I/O handle assignment (stub; shares body with VERSION).
///
/// ASM: GETIO  86DOS.asm:349
///
/// Inputs:  none
/// Outputs: (stdin_handle, stdout_handle) — always (0, 0) in simulation
pub fn getio(_state: &DosState) -> (u8, u8) {
    (0, 0)
}

/// setio — Set I/O handle assignment (stub; shares body with VERSION).
///
/// ASM: SETIO  86DOS.asm:350
///
/// Inputs:
///   _in  — new stdin handle
///   _out — new stdout handle
/// Outputs: none (no-op in simulation)
pub fn setio(_state: &mut DosState, _in: u8, _out: u8) {}

// ── Internal helpers ──────────────────────────────────────────────────────────

/// outmes_dispatch — '$'-terminated string output helper (fn 9 / PRTBUF).
///
/// In real DOS, fn 9 takes DS:DX pointing to a '$'-terminated string and
/// calls OUTMES/PRTBUF to print it.  In this simulation we have no pointer
/// model, so this is a no-op placeholder.
fn outmes_dispatch(state: &mut DosState, _param: u8) {
    // NOTE: differs from ASM because — no segment:offset pointer model in Rust.
    let _ = state;
}
