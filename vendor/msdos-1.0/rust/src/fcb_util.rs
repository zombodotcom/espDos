//! fcb_util.rs — FCB utilities, search helpers, DMA, FAT/disk pointer getters for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   GETREC      86DOS.asm:2146-2167  — convert FCB EXTENT+NR to flat record number
//!   SETRNDREC   86DOS.asm:2545-2549  — write 3-byte random-record field in FCB
//!   SRCHFRST    86DOS.asm:2302-2377  — search directory for first matching entry
//!   SAVPLCE     86DOS.asm:2306-2356  — save search position for SRCHNXT
//!   KILLSRCH    86DOS.asm:2351-2356  — clear search state
//!   SRCHDEV     86DOS.asm:2358-2376  — test if name matches a character device
//!   SRCHNXT     86DOS.asm:2378-2390  — search directory for next matching entry
//!   FILESIZE    86DOS.asm:2392-2441  — return file size from FCB
//!   SETDMA      86DOS.asm:2444-2449  — set DMA transfer address
//!   GETFATPT    86DOS.asm:2452-2469  — get FAT pointer for a drive
//!   GETDSKPT    86DOS.asm:2472-2479  — get disk-parameter-block pointer for a drive
//!   MAKEFCB     86DOS.asm:3024-3063  — build FCB from a filename string
//!   SETVECT     86DOS.asm:3116-3126  — set interrupt vector (stub in simulation)
//!   NEWBASE     86DOS.asm:3129-3185  — compute new segment base (stub in simulation)

use crate::types::{DosError, Fcb};
use crate::DosState;

// ── GETREC / SETRNDREC ────────────────────────────────────────────────────────

/// getrec_from_fcb — Convert FCB EXTENT and NR fields to a flat record number.
///
/// ASM: GETREC  86DOS.asm:2146-2167
///
/// Inputs:
///   fcb.extent — current extent (each extent = 128 records)
///   fcb.nr     — record number within extent (0-127)
/// Outputs:
///   (rec, cx) — rec = 32-bit flat record number; cx=1 (count, always)
///
/// Formula: rec = EXTENT * 128 + NR
pub fn getrec_from_fcb(fcb: &Fcb) -> (u32, u16) {
    // ASM line 2148: MOV AX,[FCB].EXTENT / MOV CL,7 / SHL AX,CL → AX=extent*128
    let rec = (fcb.extent as u32) * 128 + fcb.nr as u32;
    (rec, 1u16)
}

/// setrndrec_in_fcb — Write a 3-byte little-endian random-record number into FCB.rr.
///
/// ASM: SETRNDREC  86DOS.asm:2545-2549
///
/// Inputs:
///   fcb — mutable FCB
///   rec — 32-bit record number (only low 24 bits stored, per ASM)
/// Outputs:
///   fcb.rr[0] = rec & 0xFF
///   fcb.rr[1] = (rec >> 8) & 0xFF
///   fcb.rr[2] = (rec >> 16) & 0xFF
pub fn setrndrec_in_fcb(fcb: &mut Fcb, rec: u32) {
    // ASM line 2546: MOV [FCB].RR, AX (low word) / MOV [FCB].RR+2, BH (high byte)
    fcb.rr[0] = (rec & 0xFF) as u8;
    fcb.rr[1] = ((rec >> 8) & 0xFF) as u8;
    fcb.rr[2] = ((rec >> 16) & 0xFF) as u8;
}

/// getrrpos — Read the 3-byte random-record field from FCB as a 32-bit number.
///
/// ASM: (RR read path)  86DOS.asm:2545-2549
///
/// Inputs:
///   fcb.rr[0..3]
/// Outputs:
///   32-bit record number (little-endian, 3 bytes)
pub fn getrrpos(fcb: &Fcb) -> u32 {
    fcb.rr[0] as u32 | ((fcb.rr[1] as u32) << 8) | ((fcb.rr[2] as u32) << 16)
}

// ── Search helpers ────────────────────────────────────────────────────────────

/// srchfrst — Search directory for the first entry matching a pattern.
///
/// ASM: SRCHFRST  86DOS.asm:2302-2377
///
/// Inputs:
///   state   — DOS kernel state (last_ent reset to 0)
///   drv     — drive number
///   pattern — 11-byte name; '?' is wildcard
/// Outputs:
///   Ok(entry_index) of first match, or Err(NotFound).
pub fn srchfrst(state: &mut DosState, drv: u8, pattern: &[u8; 11]) -> Result<u16, DosError> {
    // ASM line 2304: XOR AX,AX / MOV [LASTENT],AX
    state.last_ent = 0;
    srchnxt(state, drv, pattern)
}

/// srchnxt — Search directory for the next matching entry.
///
/// ASM: SRCHNXT  86DOS.asm:2378-2390
///
/// Inputs:
///   state   — DOS kernel state (last_ent used as start)
///   drv     — drive number
///   pattern — 11-byte name with possible '?' wildcards
/// Outputs:
///   Ok(entry_index) of next match, or Err(NotFound).
pub fn srchnxt(state: &mut DosState, drv: u8, pattern: &[u8; 11]) -> Result<u16, DosError> {
    let sr = crate::directory::filsrch(state, drv, pattern)?;
    Ok(sr.entry_index)
}

/// savplce — Save current search position for SRCHNXT continuation.
///
/// ASM: SAVPLCE  86DOS.asm:2306-2356
///
/// Inputs:
///   entry_index — last matched entry ordinal
/// Outputs:
///   state.last_ent updated.
pub fn savplce(state: &mut DosState, entry_index: u16) {
    state.last_ent = entry_index;
}

/// killsrch — Clear the search state (invalidate any ongoing search).
///
/// ASM: KILLSRCH  86DOS.asm:2351-2356
pub fn killsrch(state: &mut DosState) {
    state.last_ent = 0;
}

/// srchdev — Test whether a name refers to a character device (CON/AUX/PRN).
///
/// ASM: SRCHDEV  86DOS.asm:2358-2376
///
/// Inputs:
///   name — 11-byte name array
/// Outputs:
///   true if the name matches a known device, false otherwise.
pub fn srchdev(name: &[u8; 11]) -> bool {
    crate::directory::iochk(name)
}

// ── FILESIZE ──────────────────────────────────────────────────────────────────

/// filesize — Return the file size from the FCB.
///
/// ASM: FILESIZE  86DOS.asm:2392-2441
///
/// Inputs:
///   fcb.filsiz — 32-bit file size in bytes
/// Outputs:
///   File size in bytes.
///
/// NOTE: differs from ASM because — the ASM also computes the size by walking
///       the cluster chain; here we trust the FCB field.
pub fn filesize(fcb: &Fcb) -> u32 {
    fcb.filsiz
}

// ── DMA ───────────────────────────────────────────────────────────────────────

/// setdma — Set the DMA transfer address.
///
/// ASM: SETDMA  86DOS.asm:2444-2449
///
/// Inputs:
///   off — DMA offset (BX in ASM)
///   seg — DMA segment (DS in ASM)
/// Outputs:
///   state.dma_off, state.dma_seg updated.
///
/// NOTE: differs from ASM because — in flat Rust there is no real segment; the
///       values are stored but state.dma_addr is the actual buffer.
pub fn setdma(state: &mut DosState, off: u16, seg: u16) {
    state.dma_off = off;
    state.dma_seg = seg;
}

// ── Disk / FAT pointer getters ────────────────────────────────────────────────

/// getfatpt — Return the DPB index for FAT access on the given drive.
///
/// ASM: GETFATPT  86DOS.asm:2452-2469
///
/// Inputs:
///   drv — drive number
/// Outputs:
///   Ok(index) into state.drives, or Err(InvalidDrive).
///
/// In the ASM this returns a pointer to the in-memory FAT (SI = [BP].FAT).
/// Here we return the index into state.drives; the caller accesses
/// state.drives[idx].fat directly.
pub fn getfatpt(state: &DosState, drv: u8) -> Result<usize, DosError> {
    let idx = drv as usize;
    if idx >= state.drives.len() {
        Err(DosError::InvalidDrive)
    } else {
        Ok(idx)
    }
}

/// getdskpt — Return the DPB index for a drive.
///
/// ASM: GETDSKPT  86DOS.asm:2472-2479
///
/// Inputs / Outputs: same as getfatpt (they are equivalent in this model).
pub fn getdskpt(state: &DosState, drv: u8) -> Result<usize, DosError> {
    getfatpt(state, drv)
}

// ── FCB name helpers ──────────────────────────────────────────────────────────

/// makefcb — Build a default FCB from a filename string.
///
/// ASM: MAKEFCB  86DOS.asm:3024-3063
///
/// Inputs:
///   raw — ASCIIZ or space-terminated filename bytes (DS:SI in ASM)
/// Outputs:
///   Fcb with name[0..11] set; drive=0 (default).
///   Returns Err(BadFileName) if the name is syntactically invalid.
///
/// The ASM MAKEFCB also handles drive-letter prefix ("A:name") and wildcard
/// '*'; here we delegate to directory::lodname for the 8.3 parsing.
pub fn makefcb(raw: &[u8]) -> Result<Fcb, DosError> {
    let name = crate::directory::lodname(raw)?;
    let mut fcb = Fcb::default();
    // ASM line 3030: MOV CX,11 / REP MOVSB
    fcb.name.copy_from_slice(&name);
    Ok(fcb)
}

/// getword — Read a little-endian u16 from a byte buffer at `offset`.
///
/// ASM: (inline word-fetch)  86DOS.asm:3024-3063
pub fn getword(buf: &[u8], offset: usize) -> u16 {
    if offset + 1 < buf.len() {
        u16::from_le_bytes([buf[offset], buf[offset + 1]])
    } else {
        0
    }
}

/// getlet — Return the uppercased byte at `offset` from a buffer.
///
/// ASM: (inline letter-fetch)  86DOS.asm:3024-3063
pub fn getlet(buf: &[u8], offset: usize) -> u8 {
    if offset < buf.len() {
        buf[offset].to_ascii_uppercase()
    } else {
        0
    }
}

// ── Interrupt / memory stubs ──────────────────────────────────────────────────

/// setvect — Set an interrupt vector.
///
/// ASM: SETVECT  86DOS.asm:3116-3126
///
/// Inputs:
///   int_num — interrupt number (0-255)
///   offset  — offset within CS to install
/// Outputs:
///   In the ASM: writes CS:offset into IVT entry int_num.
///   NOTE: differs from ASM because — flat Rust model; no real IVT; no-op.
pub fn setvect(_state: &mut DosState, _int_num: u8, _offset: u16) {
    // SEG CS — no-op in simulation
}

/// newbase — Compute a new segment base for relocated code.
///
/// ASM: NEWBASE  86DOS.asm:3129-3185
///
/// Inputs:
///   seg — new paragraph base
/// Outputs:
///   In the ASM: patches all inter-segment JMP/CALL targets.
///   NOTE: differs from ASM because — flat memory model; segment relocation not applicable.
pub fn newbase(_state: &mut DosState, _seg: u16) {
    // NOTE: differs from ASM because — flat memory model; segment relocation not applicable
}

/// setmem — Record the end-of-available-memory address.
///
/// ASM: (SETMEM / end-of-memory bookkeeping)  86DOS.asm:3129-3185
///
/// Inputs:
///   end — top-of-memory in paragraphs
/// Outputs:
///   state.end_mem updated.
pub fn setmem(state: &mut DosState, end: u16) {
    state.end_mem = end;
}
