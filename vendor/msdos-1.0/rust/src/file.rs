//! file.rs — File open, close, create, delete, and rename for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   OPEN        86DOS.asm:707        — open existing file, fill FCB
//!   DOOPEN      86DOS.asm:711-756    — populate FCB from directory entry
//!   OPENDEV     86DOS.asm:757-763    — open a character device as a file
//!   CLOSE       86DOS.asm:846-907    — flush dirty FCB to directory
//!   CREATE      86DOS.asm:973-1068   — create or truncate a file
//!   DELETE      86DOS.asm:602-625    — delete a file (mark entry, free clusters)
//!   RENAME      86DOS.asm:626-659    — rename a file

use crate::directory::{getfile, iochk, lodname, nextentry, putentry};
use crate::fat::{allocate, release};
use crate::types::{DosError, Dpb, Fcb, DEFAULT_RECSIZ, DEL_MARK};
use crate::DosState;

// ── OPEN ─────────────────────────────────────────────────────────────────────

/// open — Find an existing file by name and populate an FCB.
///
/// ASM: OPEN  86DOS.asm:707  (falls through to DOOPEN at 711)
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   name  — 11-byte space-padded filename (DS:DX in ASM)
/// Outputs:
///   Populated Fcb on success.
///   Err(NotFound) if the file does not exist.
///
/// The ASM OPEN calls GETFILE to locate the directory entry, then falls
/// through to DOOPEN to build the FCB.
pub fn open(state: &mut DosState, drv: u8, name: &[u8; 11]) -> Result<Fcb, DosError> {
    let result = getfile(state, drv, name)?;
    doopen(state, drv, result.entry_index)
}

/// doopen — Build an FCB from a directory entry.
///
/// ASM: DOOPEN  86DOS.asm:711-756
///
/// Inputs:
///   state       — DOS kernel state
///   drv         — drive number
///   entry_index — ordinal of the directory entry
/// Outputs:
///   Fcb with drive, name, firclus, filsiz, fdate, fildirblk, recsiz filled.
///
/// Small (16-byte) entry layout (SMALLDIR=1, 86DOS.asm line 89):
///   bytes 0-10 = name+ext, 11 = attr, 12-13 = firclus, 14-15 = file size
/// Large (32-byte) entry layout:
///   bytes 22-23 = date, 26-27 = firclus, 28-31 = file size
pub fn doopen(state: &mut DosState, drv: u8, entry_index: u16) -> Result<Fcb, DosError> {
    let raw = crate::directory::getentry(state, drv, entry_index)?;
    let mut fcb = Fcb::default();
    // ASM line 713: MOV [FCB].DRIVE, AL+1
    fcb.drive = drv + 1;
    // ASM line 715: MOV CX,11 / REP MOVSB  (copy name)
    fcb.name.copy_from_slice(&raw[..11]);
    let ent_sz = if state.drives[drv as usize].dirsiz == 0xFF {
        16usize
    } else {
        32usize
    };
    if ent_sz == 16 {
        // ASM line 720: MOV AX,[DI+12] / MOV [FCB].FIRCLUS,AX
        fcb.firclus = u16::from_le_bytes([raw[12], raw[13]]);
        fcb.filsiz = u16::from_le_bytes([raw[14], raw[15]]) as u32;
    } else {
        fcb.fdate = u16::from_le_bytes([raw[22], raw[23]]);
        fcb.firclus = u16::from_le_bytes([raw[26], raw[27]]);
        fcb.filsiz = u32::from_le_bytes([raw[28], raw[29], raw[30], raw[31]]);
    }
    // ASM line 730: MOV [FCB].LSTCLUS,AX / XOR CX,CX / MOV [FCB].CLUSPOS,CX
    fcb.lstclus = fcb.firclus;
    fcb.cluspos = 0;
    // ASM line 735: MOV [FCB].FILDIRBLK,DI  (entry index)
    fcb.fildirblk = entry_index;
    fcb.recsiz = DEFAULT_RECSIZ;
    fcb.dirtyfil = 0;
    Ok(fcb)
}

/// opendev — Open a character device (CON, AUX, PRN) as a pseudo-file.
///
/// ASM: OPENDEV  86DOS.asm:757-763
///
/// Inputs:
///   name — 11-byte device name
/// Outputs:
///   Fcb with firclus=0xFFFF (device sentinel) and default record size.
///
/// The ASM sets a special flag in the FCB to indicate device I/O; here we
/// use firclus=0xFFFF as the sentinel (checked in io::load / io::store).
pub fn opendev(name: &[u8; 11]) -> Fcb {
    let mut fcb = Fcb::default();
    fcb.name.copy_from_slice(name);
    // ASM line 759: MOV [FCB].FIRCLUS,0FFFFh
    fcb.firclus = 0xFFFF;
    fcb.recsiz = DEFAULT_RECSIZ;
    fcb
}

// ── CLOSE ────────────────────────────────────────────────────────────────────

/// close — Flush a dirty FCB back to the directory entry.
///
/// ASM: CLOSE  86DOS.asm:846-907
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   fcb   — mutable FCB; dirtyfil flag checked
/// Outputs:
///   If dirtyfil==0: no-op (file not modified).
///   Otherwise: directory entry rewritten from FCB fields; dirtyfil cleared.
///   Returns Err on I/O failure.
pub fn close(state: &mut DosState, drv: u8, fcb: &mut Fcb) -> Result<(), DosError> {
    // ASM line 848: TEST [FCB].DIRTYFIL,0FFh / JZ CLOSDONE
    if fcb.dirtyfil == 0 {
        return Ok(());
    }
    let entry_index = fcb.fildirblk;
    let raw = build_dir_entry(state, drv, fcb)?;
    putentry(state, drv, entry_index, &raw)?;
    fcb.dirtyfil = 0;
    Ok(())
}

/// badclose — Abandon a close without writing (called after I/O error).
///
/// ASM: BADCLOSE  86DOS.asm:846  (error path within CLOSE)
///
/// Inputs:
///   fcb — mutable FCB
/// Outputs:
///   dirtyfil cleared; directory entry NOT updated.
pub fn badclose(fcb: &mut Fcb) {
    fcb.dirtyfil = 0;
}

// ── CREATE ───────────────────────────────────────────────────────────────────

/// create — Create a new file or truncate an existing one.
///
/// ASM: CREATE  86DOS.asm:973-1068
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   name  — 11-byte name
/// Outputs:
///   New Fcb on success.
///   If the file already exists (EXISTENT path, line 1002), the cluster chain
///   is freed and the file is truncated to zero length.
///   If the file does not exist (FREESPOT path), a new directory slot is used.
pub fn create(state: &mut DosState, drv: u8, name: &[u8; 11]) -> Result<Fcb, DosError> {
    // ASM line 980: CALL GETFILE / JNC EXISTENT
    if let Ok(sr) = getfile(state, drv, name) {
        let mut fcb = doopen(state, drv, sr.entry_index)?;
        existent(state, drv, &mut fcb)?;
        return Ok(fcb);
    }
    // ASM line 985: CALL NEXTENTRY / JC NOSPACE
    let slot = nextentry(state, drv)?;
    freespot(state, drv, slot, name)
}

/// existent — Truncate an existing file to zero length (EXISTENT path).
///
/// ASM: EXISTENT  86DOS.asm:1002-1040  (within CREATE)
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   fcb   — FCB of the existing file (firclus, filsiz etc.)
/// Outputs:
///   Cluster chain freed (RELEASE called).
///   FCB zeroed: firclus=0, lstclus=0, cluspos=0, filsiz=0, extent=0, nr=0.
///   dirtyfil set to 1.
pub fn existent(state: &mut DosState, drv: u8, fcb: &mut Fcb) -> Result<(), DosError> {
    let first = fcb.firclus;
    // ASM line 1008: CMP [FCB].FIRCLUS,2 / JB NOCHAIN
    if first >= 2 {
        release(&mut state.drives[drv as usize], first)?;
    }
    fcb.firclus = 0;
    fcb.lstclus = 0;
    fcb.cluspos = 0;
    fcb.filsiz = 0;
    fcb.extent = 0;
    fcb.nr = 0;
    fcb.dirtyfil = 1;
    Ok(())
}

/// freespot — Initialise a new FCB at a free directory slot.
///
/// ASM: FREESPOT  86DOS.asm:1041-1068  (within CREATE)
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   slot  — directory entry index for the new file
///   name  — 11-byte filename
/// Outputs:
///   Fcb with drive, name, slot, and zeroed file-size fields.
///   Skeleton directory entry written to disk.
pub fn freespot(
    state: &mut DosState,
    drv: u8,
    slot: u16,
    name: &[u8; 11],
) -> Result<Fcb, DosError> {
    let mut fcb = Fcb::default();
    fcb.drive = drv + 1;
    fcb.name.copy_from_slice(name);
    fcb.fildirblk = slot;
    fcb.firclus = 0;
    fcb.lstclus = 0;
    fcb.filsiz = 0;
    fcb.recsiz = DEFAULT_RECSIZ;
    fcb.dirtyfil = 1;
    // ASM line 1057: write skeleton entry to directory
    let raw = build_dir_entry(state, drv, &fcb)?;
    putentry(state, drv, slot, &raw)?;
    Ok(fcb)
}

// ── DELETE ───────────────────────────────────────────────────────────────────

/// delete — Delete a named file.
///
/// ASM: DELETE  86DOS.asm:602-625
///
/// Inputs:
///   state — DOS kernel state
///   drv   — drive number
///   name  — 11-byte name (wildcards NOT supported here; use filsrch for that)
/// Outputs:
///   Cluster chain freed; directory entry first byte set to DEL_MARK (0xE5).
///   Returns Err(NotFound) if no matching file.
pub fn delete(state: &mut DosState, drv: u8, name: &[u8; 11]) -> Result<(), DosError> {
    // ASM line 604: CALL GETFILE / JC DELERR
    let sr = getfile(state, drv, name)?;
    delfile(state, drv, sr.entry_index)
}

/// delfile — Mark a directory entry deleted and free its cluster chain.
///
/// ASM: DELETE  86DOS.asm:602-625  (inner work)
///
/// Inputs:
///   state       — DOS kernel state
///   drv         — drive number
///   entry_index — directory entry ordinal
/// Outputs:
///   entry[0] set to DEL_MARK; cluster chain freed via RELEASE.
pub fn delfile(state: &mut DosState, drv: u8, entry_index: u16) -> Result<(), DosError> {
    let raw = crate::directory::getentry(state, drv, entry_index)?;
    // ASM line 609: MOV AX,[DI+12] (or +26 for large entry) → FIRCLUS
    let firclus = if state.drives[drv as usize].dirsiz == 0xFF {
        u16::from_le_bytes([raw[12], raw[13]])
    } else {
        u16::from_le_bytes([raw[26], raw[27]])
    };
    if firclus >= 2 {
        release(&mut state.drives[drv as usize], firclus)?;
    }
    // ASM line 614: MOV BYTE PTR [DI],0E5h
    let mut new_raw = raw;
    new_raw[0] = DEL_MARK;
    putentry(state, drv, entry_index, &new_raw)
}

// ── RENAME ───────────────────────────────────────────────────────────────────

/// rename — Rename a file.
///
/// ASM: RENAME  86DOS.asm:626-659
///
/// Inputs:
///   state    — DOS kernel state
///   drv      — drive number
///   old_name — existing 11-byte name
///   new_name — desired 11-byte name
/// Outputs:
///   Directory entry name field updated.
///   Err(NotFound) if old_name not found, or if new_name already exists.
///
/// NOTE: differs from ASM because — the ASM returns a specific error code for
///       "already exists"; here we reuse Err(NotFound) for simplicity.
pub fn rename(
    state: &mut DosState,
    drv: u8,
    old_name: &[u8; 11],
    new_name: &[u8; 11],
) -> Result<(), DosError> {
    // ASM line 628: CALL GETFILE (new name) / JNC RENERR (already exists)
    if getfile(state, drv, new_name).is_ok() {
        return Err(DosError::NotFound);
        // NOTE: differs from ASM because — reusing NotFound for "already exists"
    }
    let sr = getfile(state, drv, old_name)?;
    renfil(state, drv, sr.entry_index, new_name)
}

/// renfil — Overwrite the name field of a directory entry.
///
/// ASM: RENAME  86DOS.asm:626-659  (inner work, MOVNAME path)
///
/// Inputs:
///   state       — DOS kernel state
///   drv         — drive number
///   entry_index — directory entry ordinal
///   new_name    — 11-byte new filename
/// Outputs:
///   entry[0..11] replaced with new_name; entry written back to disk.
pub fn renfil(
    state: &mut DosState,
    drv: u8,
    entry_index: u16,
    new_name: &[u8; 11],
) -> Result<(), DosError> {
    let mut raw = crate::directory::getentry(state, drv, entry_index)?;
    // ASM line 650: MOV CX,11 / REP MOVSB
    raw[..11].copy_from_slice(new_name);
    putentry(state, drv, entry_index, &raw)
}

/// erret — Return the given error (mirrors ASM ERRET label).
///
/// ASM: (error return path)  86DOS.asm  various locations
pub fn erret(err: DosError) -> DosError {
    err
}

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Build a raw directory entry (16 or 32 bytes) from an FCB.
///
/// Used by close() and freespot() to serialise FCB fields into the on-disk
/// directory entry format.
fn build_dir_entry(state: &DosState, drv: u8, fcb: &Fcb) -> Result<Vec<u8>, DosError> {
    let ent_sz = if state.drives[drv as usize].dirsiz == 0xFF {
        16usize
    } else {
        32usize
    };
    let mut raw = vec![0u8; ent_sz];
    raw[..11].copy_from_slice(&fcb.name);
    if ent_sz == 16 {
        let clus = fcb.firclus.to_le_bytes();
        raw[12] = clus[0];
        raw[13] = clus[1];
        let sz = (fcb.filsiz as u16).to_le_bytes();
        raw[14] = sz[0];
        raw[15] = sz[1];
    } else {
        let dt = fcb.fdate.to_le_bytes();
        raw[22] = dt[0];
        raw[23] = dt[1];
        let clus = fcb.firclus.to_le_bytes();
        raw[26] = clus[0];
        raw[27] = clus[1];
        let sz = fcb.filsiz.to_le_bytes();
        raw[28] = sz[0];
        raw[29] = sz[1];
        raw[30] = sz[2];
        raw[31] = sz[3];
    }
    Ok(raw)
}
