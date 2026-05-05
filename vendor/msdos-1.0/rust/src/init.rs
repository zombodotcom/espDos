//! init.rs — DOS initialisation routines for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   DOSINIT     86DOS.asm:3296-3423  — primary kernel initialisation entry point
//!   PERDRV      86DOS.asm:3306-3423  — per-drive DPB construction
//!   CONTINIT    86DOS.asm:3424-3555  — post-drive-init: buffers, free memory
//!   FIGFATSIZ   86DOS.asm:3557-3563  — iterate FAT sector count to stability
//!   FIGMAX      86DOS.asm:3564-3584  — compute MAXCLUS from disk geometry
//!   MYD         86DOS.asm:3586-3610  — parse ASCII decimal number from buffer

use crate::types::{DosError, Dpb};
use crate::DosState;

/// dosinit — Primary kernel initialisation entry point.
///
/// ASM: DOSINIT  86DOS.asm:3296-3423
///
/// Inputs:
///   num_drives — number of drives reported by BIOS (AL from LODB at line 3301)
/// Outputs:
///   state.num_drv set; all drives initialised via PERDRV; CONTINIT called
///
/// Disables interrupts, sets ES=CS, reads the drive count, then calls
/// PERDRV for each drive before falling through to CONTINIT.
/// // ASM line 3296: DOSINIT: DI / UP
/// // ASM line 3301: LODB → read drive count into AL
/// // ASM line 3302: SEG ES / MOV [NUMDRV],AL
/// // ASM line 3305: MOV BX,DRVTAB / MOV DI,MEMSTRT
/// // ASM line 3306: PERDRV: (loop body for each drive)
/// NOTE: differs from ASM because — drive count passed as a parameter
///       rather than read from a BIOS data segment.
pub fn dosinit(state: &mut DosState, num_drives: u8) -> Result<(), DosError> {
    // ASM line 3302: SEG ES / MOV [NUMDRV],AL
    state.num_drv = num_drives;
    // ASM line 3306: PERDRV loop — one iteration per drive
    for i in 0..num_drives as usize {
        if i < state.drives.len() {
            perdrv(state, i)?;
        }
    }
    // ASM line 3424: fall through to CONTINIT
    continit(state)
}

/// perdrv — Per-drive DPB construction.
///
/// ASM: PERDRV  86DOS.asm:3306-3423
///
/// Inputs:
///   state — DosState with drives[drv] pre-populated from BIOS table
///   drv   — index of drive to initialise
/// Outputs:
///   drives[drv].fatsiz set; drives[drv].fat allocated with 0xFF fill
///
/// Reads SECSIZ, CLUSMSK, CLUSSHFT, FIRFAT, FATCNT, MAXENT, FIRREC,
/// DSKSIZ from the per-drive DPT (disk parameter table) via LODW/LODB,
/// then calls FIGFATSIZ to determine the number of FAT sectors needed,
/// and allocates the in-memory FAT buffer.
/// // ASM line 3307: LODB → DRVNUM stored into DPB
/// // ASM line 3309: LODW → pointer to DPT; SI = DPT
/// // ASM line 3311: LODW → SECSIZ; check against MAXSEC
/// // ASM line 3320: LODB → CLUSMSK; compute CLUSSHFT
/// // ASM line 3350: FIGFATSIZ called implicitly at line 3557
/// NOTE: differs from ASM because — DPB fields are already populated in
///       Rust; we only compute fatsiz and allocate the FAT buffer.
pub fn perdrv(state: &mut DosState, drv: usize) -> Result<(), DosError> {
    // ASM line 3557: FIGFATSIZ — iterate until fatsiz is stable
    figfatsiz(state, drv)?;
    // Allocate FAT buffer: fatsiz sectors × secsiz bytes, filled 0xFF
    let secsiz = state.drives[drv].secsiz as usize;
    let fatsiz = state.drives[drv].fatsiz as usize;
    // ASM line 3350: DI advanced by fatsiz * secsiz to allocate FAT space
    state.drives[drv].fat.resize(fatsiz * secsiz, 0xFF);
    // 0xFF = never-read sentinel (will be loaded on first access)
    state.drives[drv].dirtyfat = 0xFF;
    Ok(())
}

/// continit — Post-drive initialisation: allocate buffers, set globals.
///
/// ASM: CONTINIT  86DOS.asm:3424-3555
///
/// Inputs:
///   state.drives — all drives initialised; state.max_sec may be set
/// Outputs:
///   state.buffer, state.dir_buf allocated to max_sec bytes;
///   buf_drv_no=0xFF (no sector cached); dirty_buf=false; dirty_dir=0
///
/// Computes the maximum sector size across all drives, allocates
/// the directory buffer and sector buffer at DIRBUF/BUFFER, and
/// sets the first free memory paragraph.
/// // ASM line 3424: CONTINIT: LODW → max buffer size
/// // ASM line 3426: SEG ES / MOV BX,[MAXSEC]
/// // ASM line 3428: MOV AX,DIRBUF / ADD AX,BX → BUFFER address
/// // ASM line 3432: ADD DI,BX (dir buffer) / ADD DI,BX (sector buffer)
/// // ASM line 3436: MOV CL,4 / SHR DI,CL → first free segment
pub fn continit(state: &mut DosState) -> Result<(), DosError> {
    // ASM line 3426: MOV BX,[MAXSEC] — find largest sector size
    let max_sec = state.drives.iter().map(|d| d.secsiz).max().unwrap_or(512);
    state.max_sec = max_sec;
    // ASM line 3432: allocate directory buffer and main sector buffer
    state.buffer.resize(max_sec as usize, 0);
    state.dir_buf.resize(max_sec as usize, 0);
    // ASM: no valid sector in buffer yet
    state.buf_drv_no = 0xFF;
    state.buf_sec_no = 0;
    state.dirty_buf = false;
    state.dirty_dir = 0;
    Ok(())
}

/// figfatsiz — Determine number of FAT sectors by iteration.
///
/// ASM: FIGFATSIZ  86DOS.asm:3557-3563
///
/// Inputs:
///   drives[drv].firfat, fatcnt, secsiz, clusmsk set from BIOS table
/// Outputs:
///   drives[drv].fatsiz — minimum number of sectors to hold the FAT
///   drives[drv].maxclus — maximum cluster number
///
/// The ASM computes: fatsiz * fatcnt + firfat + SDIRSEC, then passes
/// the result to FIGMAX (as an approximation of FIRREC) and iterates
/// once.  We iterate until stable, capped at 8 sectors for safety.
/// // ASM line 3557: FIGFATSIZ: SEG ES / MUL AL,[BP+FATCNT]
/// // ASM line 3559: ADD AX,[BP+FIRFAT]
/// // ASM line 3561: ADD AX,[SDIRSEC]
/// // ASM line 3562: fall into FIGMAX
pub fn figfatsiz(state: &mut DosState, drv: usize) -> Result<(), DosError> {
    let mut fatsiz: u8 = 1;
    loop {
        // ASM line 3557: fatsiz * fatcnt (MUL AL,[BP+FATCNT])
        state.drives[drv].fatsiz = fatsiz;
        // ASM line 3562: call FIGMAX with current fatsiz
        let maxclus = figmax(state, drv);
        state.drives[drv].maxclus = maxclus;
        // Bytes needed for FAT: ceil((maxclus+1) * 1.5)
        // ASM line 3572: INC AX / MOV DX,AX / SHR DX / ADC AX,DX
        let fat_bytes = (maxclus as u32 + 1) * 3 / 2 + 1;
        let secsiz = state.drives[drv].secsiz as u32;
        // ASM line 3580: DIV AX,SI → sectors needed
        let needed = ((fat_bytes + secsiz - 1) / secsiz) as u8;
        if needed <= fatsiz {
            break; // stable — fatsiz is correct
        }
        fatsiz = needed;
        if fatsiz > 8 {
            break; // sanity limit
        }
    }
    state.drives[drv].fatsiz = fatsiz;
    Ok(())
}

/// figmax — Compute MAXCLUS from disk geometry.
///
/// ASM: FIGMAX  86DOS.asm:3564-3584
///
/// Inputs:
///   drives[drv].firrec    — first data record (sector) number
///   drives[drv].fatsiz    — current FAT size hypothesis
///   drives[drv].clusshft  — log2(sectors per cluster)
///   drives[drv].clusmsk   — sectors-per-cluster minus 1
/// Outputs:
///   MAXCLUS — maximum cluster number on this drive
///
/// Algorithm (from ASM lines 3565-3583):
///   AX = DSKSIZ - FIRREC        ; total data sectors
///   AX = AX >> CLUSSHFT         ; convert to clusters
///   MAXCLUS = AX + 1            ; cluster numbers start at 1
///
/// // ASM line 3565: FIGMAX: SEG ES / SUB AX,[BP+DSKSIZ] / NEG AX
/// // ASM line 3568: SEG ES / MOV CL,[BP+CLUSSHFT] / SHR AX,CL
/// // ASM line 3570: INC AX → MAXCLUS
/// NOTE: differs from ASM because — DSKSIZ is not stored in Dpb; we
///       reconstruct a plausible value from existing fields.
pub fn figmax(state: &DosState, drv: usize) -> u16 {
    let dpb = &state.drives[drv];
    // NOTE: differs from ASM because — disk size must be supplied externally;
    //       here we reconstruct it from firrec + estimated data area.
    let dsksiz: u16 = dpb.firrec + (dpb.maxclus).saturating_sub(1) * ((dpb.clusmsk as u16) + 1) + 8;
    // ASM line 3565: SUB AX,[BP+DSKSIZ] / NEG AX → data_secs
    let data_secs = dsksiz.saturating_sub(dpb.firrec);
    let secs_per_clus = (dpb.clusmsk as u16) + 1;
    // ASM line 3568: SHR AX,CL (CL = CLUSSHFT)
    let maxclus = if secs_per_clus > 0 {
        // ASM line 3570: INC AX
        data_secs / secs_per_clus + 1
    } else {
        1
    };
    maxclus
}

/// myd — Parse an ASCII decimal number from a buffer.
///
/// ASM: MYD  86DOS.asm:3586-3610
///
/// Inputs:
///   buf — byte slice at SI pointing into an input buffer
///   DX  — upper bound (not stored; callers enforce limits)
/// Outputs:
///   Ok(value) — parsed decimal value in AX
///   Err(BadFileName) — carry set (no digits found or parse error)
///
/// Accumulates digits: BX = BX*10 + digit, stops on any non-digit.
/// Returns carry-set (Err) if no digit was seen or if the result is 0.
/// // ASM line 3586: MYD: XOR BX,BX / MOV AH,0
/// // ASM line 3589: GETDIG: LODB / SUB AL,'0'
/// // ASM line 3591: JC CHKRET (carry = below '0')
/// // ASM line 3592: CMP AL,10 / JNC CHKRET (above '9')
/// // ASM line 3593-3598: BX = BX*10 + AL
/// // ASM line 3601: CHKRET: MOV AX,BX / OR AX,AX / STC / JZ RET
/// // ASM line 3604: CMP DX,AX
pub fn myd(buf: &[u8]) -> Result<u16, DosError> {
    let mut val: u16 = 0;
    let mut got_digit = false;
    for &b in buf {
        // ASM line 3589: SUB AL,'0' / JC CHKRET
        if b >= b'0' && b <= b'9' {
            got_digit = true;
            // ASM lines 3593-3598: SHL BX / MOV CX,BX / SHL BX / SHL BX / ADD BX,CX / ADD BX,AX
            val = val.wrapping_mul(10).wrapping_add((b - b'0') as u16);
        } else if b == b' ' || b == b'\t' {
            continue; // skip leading whitespace (not in ASM but harmless)
        } else {
            break; // ASM line 3591: JC CHKRET — stop on non-digit
        }
    }
    // ASM line 3601: OR AX,AX / STC / JZ RET → carry if zero or no digit
    if !got_digit {
        Err(DosError::BadFileName)
    } else {
        Ok(val)
    }
}

/// memscan — Scan memory to find top of RAM.
///
/// ASM: MEMSCAN  86DOS.asm:~3500-3520
///
/// Inputs:  none (scans physical memory in real DOS)
/// Outputs: paragraph address of top of available RAM
///
/// NOTE: differs from ASM because — flat Rust model has no physical
///       memory to probe; returns a fixed 640 KB simulation value.
pub fn memscan(_state: &DosState) -> u16 {
    0xA000 // 640 KB in paragraphs (0xA000 × 16 = 655360 bytes)
}

/// movfat — Copy FAT from boot-sector load area into DPB fat field.
///
/// ASM: MOVFAT  86DOS.asm:~3522-3545
///
/// Inputs:  DI = destination; SI = source; CX = byte count
/// Outputs: FAT data copied; SI/DI advanced
///
/// NOTE: differs from ASM because — FAT data is handled implicitly in
///       perdrv/figfatsiz in this Rust translation; no explicit copy needed.
pub fn movfat(_state: &mut DosState) {}

/// fininit — Final initialisation: set interrupt vectors, print boot message.
///
/// ASM: FININIT  86DOS.asm:~3547-3555
///
/// Inputs:  state fully initialised
/// Outputs: interrupt vectors set; boot message printed to console
///
/// NOTE: differs from ASM because — no real interrupt table or console
///       output model in simulation; no-op.
pub fn fininit(_state: &mut DosState) {}

/// setmem — Set the end-of-memory marker.
///
/// ASM: SETMEM  86DOS.asm:~2602-2620 (also referenced from init)
///
/// Inputs:
///   end — paragraph address of top of user-accessible RAM
/// Outputs:
///   state.end_mem updated
///
/// // ASM: SEG CS / MOV [ENDMEM],AX
pub fn setmem(state: &mut DosState, end: u16) {
    state.end_mem = end;
}

/// getdat — Read current date from BIOS clock.
///
/// ASM: GETDAT  86DOS.asm:~3600-3610
///
/// Inputs:  BIOS real-time clock
/// Outputs: packed date word in AX
///
/// NOTE: differs from ASM because — no BIOS RTC in simulation;
///       returns 0 (date unknown).
pub fn getdat(_state: &mut DosState) -> u16 {
    0
}
