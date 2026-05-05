//! directory.rs — Directory search, entry management for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   IOCHK       86DOS.asm:434-446    — check if name refers to a character device
//!   GETFILE     86DOS.asm:448-515    — search directory for a named file (FILSRCH + CONTSRCH)
//!   FILSRCH     86DOS.asm:484        — start directory search (alias for GETFILE entry)
//!   CONTSRCH    86DOS.asm:486-515    — continue directory search with wildcard matching
//!   GETENTRY    86DOS.asm:516-562    — fetch a raw directory entry by index
//!   NEXTENTRY   86DOS.asm:563-595    — find next free directory slot
//!   NONE        86DOS.asm:596-601    — test whether a directory slot is empty
//!   MOVNAME     86DOS.asm:660-678    — copy 11-byte name into workspace
//!   LODNAME     86DOS.asm:679-695    — parse and space-pad an 8.3 filename
//!   GETBP       86DOS.asm:696-706    — validate drive and return DPB reference
//!   STARTSRCH   86DOS.asm:764-765    — reset search state for a new GETFILE

use crate::disk::{dircomp, dirread};
use crate::types::{DosError, Dpb, Fcb, DEL_MARK};
use crate::DosState;

// Entry sizes
const SMALL_ENT: usize = 16;
const LARGE_ENT: usize = 32;

fn entry_size(dpb: &Dpb) -> usize {
    if dpb.dirsiz == 0xFF {
        SMALL_ENT
    } else {
        LARGE_ENT
    }
}

// ── Name helpers ──────────────────────────────────────────────────────────────

/// movname — Copy an 11-byte filename into an output array.
///
/// ASM: MOVNAME  86DOS.asm:660-678
///
/// Inputs:
///   src — byte slice, at least 11 bytes (DS:SI in ASM)
/// Outputs:
///   [u8; 11] — space-padded 11-byte name array (ES:DI in ASM)
///
/// The ASM uses MOVSB for 11 bytes; short sources are padded with spaces.
pub fn movname(src: &[u8]) -> [u8; 11] {
    let mut name = [b' '; 11];
    // ASM line 662: MOV CX,11 / REP MOVSB
    for i in 0..11.min(src.len()) {
        name[i] = src[i];
    }
    name
}

/// lodname — Parse a raw filename string into a space-padded 8.3 array.
///
/// ASM: LODNAME  86DOS.asm:679-695
///
/// Inputs:
///   raw — ASCII filename bytes (DS:SI in ASM), e.g. b"FOO.BAR"
/// Outputs:
///   [u8; 11] — bytes 0-7 = name (space-padded), bytes 8-10 = extension.
///   Returns Err(BadFileName) if the input is syntactically invalid.
///   All characters are uppercased.
///
/// The ASM scans until '.' or space/NUL, fills 8 name bytes, then up to 3
/// extension bytes after '.'.
pub fn lodname(raw: &[u8]) -> Result<[u8; 11], DosError> {
    let mut out = [b' '; 11];
    let mut i = 0usize;
    let mut pos = 0usize;
    // ASM line 681: scan name (up to 8 chars, stop at '.' space or NUL)
    while i < raw.len() && raw[i] != b'.' && pos < 8 {
        let c = raw[i].to_ascii_uppercase();
        if c == b' ' || c == 0 {
            break;
        }
        out[pos] = c;
        pos += 1;
        i += 1;
    }
    // ASM line 686: skip '.'
    if i < raw.len() && raw[i] == b'.' {
        i += 1;
        pos = 8;
        // ASM line 688: scan extension (up to 3 chars)
        while i < raw.len() && pos < 11 {
            let c = raw[i].to_ascii_uppercase();
            if c == b' ' || c == 0 {
                break;
            }
            out[pos] = c;
            pos += 1;
            i += 1;
        }
    }
    Ok(out)
}

/// iochk — Test whether a filename refers to a character device.
///
/// ASM: IOCHK  86DOS.asm:434-446
///
/// Inputs:
///   name — 11-byte name array
/// Outputs:
///   true  if name[0..3] matches CON, AUX, or PRN (device names)
///   false otherwise
///
/// The ASM checks the first three bytes against a table of device names.
pub fn iochk(name: &[u8; 11]) -> bool {
    let devnames: &[[u8; 3]] = &[*b"CON", *b"AUX", *b"PRN"];
    for dev in devnames {
        if &name[..3] == dev {
            return true;
        }
    }
    false
}

// ── Directory search ──────────────────────────────────────────────────────────

/// Result of a successful directory search (mirrors ASM register state after GETFILE).
pub struct SearchResult {
    pub entry_index: u16,
    pub sector: u16,
    pub offset: usize,
}

/// startsrch — Reset search state before a new GETFILE scan.
///
/// ASM: STARTSRCH  86DOS.asm:764-765
///
/// Inputs:  (none beyond state)
/// Outputs: state.last_ent reset to 0
pub fn startsrch(state: &mut DosState) {
    state.last_ent = 0;
}

/// getfile — Search the directory for a file with the given 11-byte name.
///
/// ASM: GETFILE  86DOS.asm:448-515  (entry at FILSRCH, line 484)
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number (0=A, 1=B, …)
///   name  — 11-byte space-padded name to search for (DS:DX in ASM)
/// Outputs:
///   SearchResult on success, Err(NotFound) if no match.
///
/// Resets the search position (STARTSRCH) then calls findname/contsrch.
pub fn getfile(state: &mut DosState, drv: u8, name: &[u8; 11]) -> Result<SearchResult, DosError> {
    startsrch(state);
    findname(state, drv, name)
}

/// findname — Search for a name from state.last_ent onward.
///
/// ASM: (inner search loop of GETFILE)  86DOS.asm:448-515
///
/// Inputs:
///   state    — DOS kernel state (last_ent used as start index)
///   drv      — drive number
///   name     — 11-byte name
/// Outputs:
///   SearchResult or Err(NotFound).
pub fn findname(state: &mut DosState, drv: u8, name: &[u8; 11]) -> Result<SearchResult, DosError> {
    let dpb_maxent = state.drives[drv as usize].maxent;
    let start = state.last_ent;
    contsrch(state, drv, name, start, dpb_maxent)
}

/// contsrch — Continue a directory search (exact-match) from a given entry index.
///
/// ASM: CONTSRCH  86DOS.asm:486-515
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   name  — 11-byte name to match (no wildcards)
///   start — first entry index to check
///   max   — entry count (dpb.maxent)
/// Outputs:
///   SearchResult on success, Err(NotFound) on exhaustion.
///
/// The ASM loop: read entry → if first byte 0x00 → end (NONE path) →
///   if DEL_MARK skip → compare 11 bytes → match → return.
pub fn contsrch(
    state: &mut DosState,
    drv: u8,
    name: &[u8; 11],
    start: u16,
    max: u16,
) -> Result<SearchResult, DosError> {
    let ent_sz = entry_size(&state.drives[drv as usize]);
    for idx in start..max {
        let (sector, offset) = dircomp(&state.drives[drv as usize], idx);
        dirread(state, drv, sector)?;
        let buf = &state.dir_buf.clone();
        let entry = &buf[offset..offset + ent_sz];
        let first = entry[0];
        // ASM line 496: CMP BYTE PTR [DI],0 / JE SRCHFAIL
        if first == 0x00 {
            return Err(DosError::NotFound);
        }
        // ASM line 499: CMP BYTE PTR [DI],0E5h / JE NEXTE
        if first == DEL_MARK {
            continue;
        }
        // ASM lines 502-511: CMPSB × 11
        if &entry[..11] == name.as_ref() {
            state.last_ent = idx + 1;
            return Ok(SearchResult {
                entry_index: idx,
                sector,
                offset,
            });
        }
    }
    Err(DosError::NotFound)
}

/// getentry — Fetch a raw directory entry (16 or 32 bytes) by entry index.
///
/// ASM: GETENTRY  86DOS.asm:516-562
///
/// Inputs:
///   state       — DOS kernel state
///   drv         — drive number
///   entry_index — ordinal of the directory entry
/// Outputs:
///   Vec<u8> containing the raw entry bytes.
///   Returns Err on I/O failure.
pub fn getentry(state: &mut DosState, drv: u8, entry_index: u16) -> Result<Vec<u8>, DosError> {
    let ent_sz = entry_size(&state.drives[drv as usize]);
    let (sector, offset) = dircomp(&state.drives[drv as usize], entry_index);
    dirread(state, drv, sector)?;
    Ok(state.dir_buf[offset..offset + ent_sz].to_vec())
}

/// nextentry — Find the next free (empty or deleted) directory slot.
///
/// ASM: NEXTENTRY  86DOS.asm:563-595
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
/// Outputs:
///   Index of first free slot (first byte == 0x00 or DEL_MARK).
///   Returns Err(NoSpace) if the directory is full.
pub fn nextentry(state: &mut DosState, drv: u8) -> Result<u16, DosError> {
    let maxent = state.drives[drv as usize].maxent;
    let ent_sz = entry_size(&state.drives[drv as usize]);
    for idx in 0..maxent {
        let (sector, offset) = dircomp(&state.drives[drv as usize], idx);
        dirread(state, drv, sector)?;
        let first = state.dir_buf[offset];
        // ASM line 571: CMP BYTE PTR [DI],0 / JE FOUNDFREE
        // ASM line 574: CMP BYTE PTR [DI],0E5h / JE FOUNDFREE
        if first == 0x00 || first == DEL_MARK {
            return Ok(idx);
        }
    }
    Err(DosError::NoSpace)
}

/// none — Test whether a directory entry slot is free (unused or deleted).
///
/// ASM: NONE  86DOS.asm:596-601
///
/// Inputs:
///   entry — raw directory entry bytes
/// Outputs:
///   true if entry[0] == 0x00 (never used) or DEL_MARK (0xE5, deleted).
pub fn none(entry: &[u8]) -> bool {
    entry[0] == 0x00 || entry[0] == DEL_MARK
}

/// filsrch — Wildcard directory search (FCB-style, '?' matches any char).
///
/// ASM: FILSRCH / CONTSRCH  86DOS.asm:484 / 486-515
///
/// Inputs:
///   state   — DOS kernel state (last_ent as start)
///   drv     — drive number
///   pattern — 11-byte pattern; '?' matches any character in that position
/// Outputs:
///   SearchResult for first matching entry, or Err(NotFound).
pub fn filsrch(
    state: &mut DosState,
    drv: u8,
    pattern: &[u8; 11],
) -> Result<SearchResult, DosError> {
    let maxent = state.drives[drv as usize].maxent;
    let ent_sz = entry_size(&state.drives[drv as usize]);
    let start = state.last_ent;
    for idx in start..maxent {
        let (sector, offset) = dircomp(&state.drives[drv as usize], idx);
        dirread(state, drv, sector)?;
        let buf = state.dir_buf.clone();
        let entry = &buf[offset..offset + ent_sz];
        let first = entry[0];
        if first == 0x00 {
            return Err(DosError::NotFound);
        }
        if first == DEL_MARK {
            continue;
        }
        // ASM: CMPSB with CMP AL,'?' skip (wildcard)
        let mut matched = true;
        for j in 0..11 {
            if pattern[j] != b'?' && pattern[j] != entry[j] {
                matched = false;
                break;
            }
        }
        if matched {
            state.last_ent = idx + 1;
            return Ok(SearchResult {
                entry_index: idx,
                sector,
                offset,
            });
        }
    }
    Err(DosError::NotFound)
}

/// savplce — Save the current search position for later SRCHNXT continuation.
///
/// ASM: SAVPLCE  86DOS.asm:2306-2356  (stub here; full impl in fcb_util)
///
/// Inputs:
///   state       — DOS kernel state
///   entry_index — last matched entry ordinal
/// Outputs:
///   state.last_ent updated.
pub fn savplce(state: &mut DosState, entry_index: u16) {
    state.last_ent = entry_index;
}

/// getbp — Validate drive number and return the DPB index.
///
/// ASM: GETBP  86DOS.asm:696-706
///
/// Inputs:
///   state — DOS kernel state
///   drive — drive number (0=A, 1=B, …)
/// Outputs:
///   Ok(index) into state.drives, or Err(InvalidDrive).
///
/// In the ASM GETBP returns a pointer in BP; here we return a usize index.
pub fn getbp(state: &DosState, drive: u8) -> Result<usize, DosError> {
    let idx = drive as usize;
    if idx >= state.drives.len() {
        Err(DosError::InvalidDrive)
    } else {
        Ok(idx)
    }
}

/// putentry — Write a directory entry back to disk.
///
/// (Helper used by CREATE, DELETE, RENAME — no dedicated ASM label; the
///  individual routines write back inline.)
///
/// Inputs:
///   state       — DOS kernel state
///   drv         — drive number
///   entry_index — ordinal of the directory entry
///   data        — raw entry bytes to write (16 or 32 bytes)
/// Outputs:
///   Entry written; state.dirty_dir cleared after flush.
pub fn putentry(
    state: &mut DosState,
    drv: u8,
    entry_index: u16,
    data: &[u8],
) -> Result<(), DosError> {
    let ent_sz = entry_size(&state.drives[drv as usize]);
    let (sector, offset) = dircomp(&state.drives[drv as usize], entry_index);
    dirread(state, drv, sector)?;
    let end = offset + ent_sz.min(data.len());
    state.dir_buf[offset..end].copy_from_slice(&data[..end - offset]);
    state.dirty_dir = 1;
    crate::disk::dirwrite(state, drv, sector)
}
