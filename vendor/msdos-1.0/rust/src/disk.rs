//! disk.rs — Disk read/write, sector buffering, directory sector I/O.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   BUFSEC      86DOS.asm:1508-1565  — ensure a sector is in the single-sector buffer
//!   BUFRD       86DOS.asm:1567-1579  — read one sector into the buffer
//!   BUFWRT      86DOS.asm:1581-1606  — write the buffered sector to disk
//!   NEXTSEC     86DOS.asm:1608-1630  — compute next logical sector in cluster chain
//!   HARDREAD    86DOS.asm:1123-1127  — multi-sector BIOS read
//!   HARDWRITE   86DOS.asm:1180-1184  — multi-sector BIOS write
//!   HARDERR     86DOS.asm:1186-1214  — handle a BIOS error
//!   DREAD       86DOS.asm:1095-1122  — logical-sector read via buffer
//!   DWRITE      86DOS.asm:1150-1179  — logical-sector write via buffer
//!   DIRREAD     86DOS.asm:1071-1094  — read a directory sector into dir_buf
//!   DIRWRITE    86DOS.asm:1133-1149  — write dir_buf back to disk
//!   CHKDIRWRITE 86DOS.asm:1129-1132  — conditional DIRWRITE
//!   DIRCOMP     86DOS.asm:963-972    — compute sector + byte-offset for a dir entry
//!   FIGREC      86DOS.asm:2126-2145  — cluster+sector offset from byte position
//!   BREAKDOWN   86DOS.asm:1433-1463  — split byte range into partial/whole sectors
//!   OPTIMIZE    86DOS.asm:2055-2123  — merge adjacent sector transfers

use crate::types::{DosError, Dpb};
use crate::DosState;

// ── Sector buffer helpers ─────────────────────────────────────────────────────

/// bufsec — Ensure sector `sec` of drive `drv` is in the single-sector buffer.
///
/// ASM: BUFSEC  86DOS.asm:1508-1565
///
/// Inputs:
///   state.buf_sec_no — sector currently in buffer
///   state.buf_drv_no — drive of buffered sector
///   drv, sec         — desired drive and sector
/// Outputs:
///   If already buffered: no-op.
///   If dirty: calls BUFWRT first (lines 1515-1519).
///   Then calls BUFRD to fill the buffer.
pub fn bufsec(state: &mut DosState, drv: u8, sec: u16) -> Result<(), DosError> {
    // ASM line 1510: CMP [BUFSECNO],AX / JNE NEEDBUF (also check drive)
    if state.buf_drv_no == drv && state.buf_sec_no == sec {
        return Ok(());
    }
    // ASM line 1515: TEST [DIRTYBUF],0FFh / JZ NEEDBUF
    if state.dirty_buf {
        bufwrt(state)?;
    }
    bufrd(state, drv, sec)
}

/// bufrd — Read one sector from disk into the sector buffer.
///
/// ASM: BUFRD  86DOS.asm:1567-1579
///
/// Inputs:
///   drv — drive number
///   sec — logical sector number
/// Outputs:
///   state.buffer filled with sector data
///   state.buf_drv_no, state.buf_sec_no updated
///   state.dirty_buf cleared
///   Returns Err(DiskError) on BIOS error.
pub fn bufrd(state: &mut DosState, drv: u8, sec: u16) -> Result<(), DosError> {
    let secsiz = state.drives[drv as usize].secsiz as usize;
    state.buffer.resize(secsiz, 0);
    // ASM line 1571: CALL BIOS (INT 25h / disk read)
    let carry = state.bios.disk_read(drv, &mut state.buffer, sec, 1);
    if carry {
        return Err(DosError::DiskError);
    }
    state.buf_drv_no = drv;
    state.buf_sec_no = sec;
    state.dirty_buf = false;
    Ok(())
}

/// bufwrt — Write the sector buffer to disk.
///
/// ASM: BUFWRT  86DOS.asm:1581-1606
///
/// Inputs:
///   state.buffer     — data to write
///   state.buf_drv_no — target drive
///   state.buf_sec_no — target sector
/// Outputs:
///   Sector written to disk; state.dirty_buf cleared.
///   Returns Err(DiskError) on BIOS error.
pub fn bufwrt(state: &mut DosState) -> Result<(), DosError> {
    let drv = state.buf_drv_no;
    let sec = state.buf_sec_no;
    // ASM line 1585: CALL BIOS (INT 26h / disk write)
    let buf = state.buffer.clone();
    let carry = state.bios.disk_write(drv, &buf, sec, 1);
    if carry {
        return Err(DosError::DiskError);
    }
    state.dirty_buf = false;
    Ok(())
}

// ── Hard read / write (multi-sector) ─────────────────────────────────────────

/// hardread — Multi-sector BIOS disk read.
///
/// ASM: HARDREAD  86DOS.asm:1123-1127
///
/// Inputs:
///   drv, sector, count — drive number, start sector, sector count
///   buf                — output buffer (must be pre-sized)
/// Outputs:
///   buf filled with sector data.
///   Returns Err(DiskError) if BIOS signals carry.
pub fn hardread(
    state: &mut DosState,
    drv: u8,
    buf: &mut [u8],
    sector: u16,
    count: u16,
) -> Result<(), DosError> {
    // ASM line 1124: CALL BIOS
    let carry = state.bios.disk_read(drv, buf, sector, count);
    if carry {
        Err(DosError::DiskError)
    } else {
        Ok(())
    }
}

/// hardwrite — Multi-sector BIOS disk write.
///
/// ASM: HARDWRITE  86DOS.asm:1180-1184
///
/// Inputs:
///   drv, sector, count — drive number, start sector, sector count
///   buf                — data to write
/// Outputs:
///   Data written to disk.
///   Returns Err(DiskError) if BIOS signals carry.
pub fn hardwrite(
    state: &mut DosState,
    drv: u8,
    buf: &[u8],
    sector: u16,
    count: u16,
) -> Result<(), DosError> {
    // ASM line 1181: CALL BIOS
    let carry = state.bios.disk_write(drv, buf, sector, count);
    if carry {
        Err(DosError::DiskError)
    } else {
        Ok(())
    }
}

/// harderr — Record a BIOS-level disk error.
///
/// ASM: HARDERR  86DOS.asm:1186-1214
///
/// Inputs:
///   carry set by BIOS call (implicit in Rust — called only on error path)
/// Outputs:
///   state.dskerr set to 1.
///   Returns DosError::DiskError to propagate to caller.
pub fn harderr(state: &mut DosState) -> DosError {
    state.dskerr = 1;
    DosError::DiskError
}

// ── DREAD / DWRITE (logical sector I/O using buffer) ─────────────────────────

/// dread — Read a logical sector into the sector buffer.
///
/// ASM: DREAD  86DOS.asm:1095-1122
///
/// Inputs:
///   drv    — drive number
///   sector — logical sector number
/// Outputs:
///   state.buffer contains the requested sector (via bufsec).
pub fn dread(state: &mut DosState, drv: u8, sector: u16) -> Result<(), DosError> {
    bufsec(state, drv, sector)
}

/// dwrite — Mark a logical sector in the buffer as dirty (deferred write).
///
/// ASM: DWRITE  86DOS.asm:1150-1179
///
/// Inputs:
///   drv    — drive number
///   sector — logical sector number
/// Outputs:
///   state.buf_drv_no, state.buf_sec_no, state.dirty_buf set.
///   Actual write is deferred to next bufsec/bufwrt call.
/// NOTE: differs from ASM because — ASM writes immediately; here we defer for
///       better simulation throughput.
pub fn dwrite(state: &mut DosState, drv: u8, sector: u16) -> Result<(), DosError> {
    state.buf_drv_no = drv;
    state.buf_sec_no = sector;
    state.dirty_buf = true;
    Ok(())
}

// ── Directory sector read/write ───────────────────────────────────────────────

/// dirread — Read a directory sector into the directory buffer.
///
/// ASM: DIRREAD  86DOS.asm:1071-1094
///
/// Inputs:
///   drv    — drive number
///   sector — directory sector number
/// Outputs:
///   state.dir_buf filled with the requested sector.
///   state.dir_buf_id updated.
///   state.dirty_dir cleared.
///   Returns Err(DiskError) on I/O failure.
///
/// If the requested sector is already buffered, returns immediately (line 1074).
/// If dirty, flushes first (lines 1076-1079).
pub fn dirread(state: &mut DosState, drv: u8, sector: u16) -> Result<(), DosError> {
    // ASM line 1074: CMP [DIRBUFID],AX / JE DIRALRDY
    if state.dir_buf_id == sector && state.buf_drv_no == drv {
        return Ok(());
    }
    // ASM line 1076: TEST [DIRTYDIR],0FFh / JZ NOTDIRTYDIR
    if state.dirty_dir != 0 {
        dirwrite(state, drv, state.dir_buf_id)?;
    }
    let secsiz = state.drives[drv as usize].secsiz as usize;
    state.dir_buf.resize(secsiz, 0);
    // ASM line 1082: CALL BIOS
    let carry = state.bios.disk_read(drv, &mut state.dir_buf, sector, 1);
    if carry {
        return Err(DosError::DiskError);
    }
    state.dir_buf_id = sector;
    state.dirty_dir = 0;
    Ok(())
}

/// dirwrite — Write the directory buffer back to disk.
///
/// ASM: DIRWRITE  86DOS.asm:1133-1149
///
/// Inputs:
///   state.dir_buf — directory sector data
///   drv, sector   — target drive and sector
/// Outputs:
///   Directory sector written; state.dirty_dir cleared.
///   Returns Err(DiskError) on I/O failure.
pub fn dirwrite(state: &mut DosState, drv: u8, sector: u16) -> Result<(), DosError> {
    let buf = state.dir_buf.clone();
    // ASM line 1137: CALL BIOS
    let carry = state.bios.disk_write(drv, &buf, sector, 1);
    if carry {
        return Err(DosError::DiskError);
    }
    state.dirty_dir = 0;
    Ok(())
}

/// chkdirwrite — Conditionally flush the dirty directory buffer.
///
/// ASM: CHKDIRWRITE  86DOS.asm:1129-1132
///
/// Inputs:
///   state.dirty_dir — nonzero if dir_buf needs writing
/// Outputs:
///   Calls dirwrite if dirty; otherwise no-op.
pub fn chkdirwrite(state: &mut DosState) -> Result<(), DosError> {
    // ASM line 1130: TEST [DIRTYDIR],0FFh / JZ NODIRWRT
    if state.dirty_dir != 0 {
        let drv = state.buf_drv_no;
        let sec = state.dir_buf_id;
        dirwrite(state, drv, sec)?;
    }
    Ok(())
}

/// dircomp — Compute the sector number and byte offset of a directory entry.
///
/// ASM: DIRCOMP  86DOS.asm:963-972
///
/// Inputs:
///   dpb         — drive parameter block
///   entry_index — ordinal of the directory entry (0-based)
/// Outputs:
///   (sector, byte_offset) — sector within the directory area and byte offset
///                            within that sector.
///
/// Entry size is 16 bytes when dpb.dirsiz==0xFF (SMALLDIR=1), else 32 bytes.
pub fn dircomp(dpb: &Dpb, entry_index: u16) -> (u16, usize) {
    // ASM line 964: SMALLDIR EQU 1  → 16-byte entries
    let entry_size = if dpb.dirsiz == 0xFF { 16usize } else { 32usize };
    let entries_per_sec = dpb.secsiz as usize / entry_size;
    let sec_offset = entry_index as usize / entries_per_sec;
    let byte_offset = (entry_index as usize % entries_per_sec) * entry_size;
    // ASM line 968: ADD AX,[BP].FIRDIR
    let sector = dpb.firdir + sec_offset as u16;
    (sector, byte_offset)
}

// ── Logical sector helpers ────────────────────────────────────────────────────

/// nextsec — Compute the logical sector number for a position within a cluster.
///
/// ASM: NEXTSEC  86DOS.asm:1608-1630
///
/// Inputs:
///   dpb            — drive parameter block
///   cluster        — cluster number (BX in ASM)
///   sec_in_cluster — sector index within cluster (0-based; SI in ASM)
/// Outputs:
///   Logical sector number: firrec + (cluster-2)*secs_per_cluster + sec_in_cluster
pub fn nextsec(dpb: &Dpb, cluster: u16, sec_in_cluster: u8) -> u16 {
    // ASM line 1612: MOV AX,BX / DEC AX / DEC AX  → cluster - 2
    let secs_per_cluster = (dpb.clusmsk as u16) + 1;
    let base = dpb.firrec + (cluster - 2) * secs_per_cluster;
    base + sec_in_cluster as u16
}

/// figrec — Convert a byte position in a file to (cluster_offset, sec_in_cluster, byte_in_sector).
///
/// ASM: FIGREC  86DOS.asm:2126-2145
///
/// Inputs:
///   dpb      — drive parameter block
///   byte_pos — absolute byte position within the file (BX:CX in ASM)
/// Outputs:
///   cluster_offset  — how many clusters from the start of the file (to pass to FNDCLUS)
///   sec_in_cluster  — sector index within that cluster
///   byte_in_sector  — byte offset within that sector
pub fn figrec(dpb: &Dpb, byte_pos: u32) -> (u16, u8, u16) {
    let secsiz = dpb.secsiz as u32;
    let secs_per_cluster = (dpb.clusmsk as u32) + 1;
    let cluster_bytes = secsiz * secs_per_cluster;
    // ASM line 2130: DIV cluster_bytes
    let cluster_offset = (byte_pos / cluster_bytes) as u16;
    let rem = byte_pos % cluster_bytes;
    // ASM line 2135: DIV secsiz
    let sec_in_cluster = (rem / secsiz) as u8;
    let byte_in_sector = (rem % secsiz) as u16;
    (cluster_offset, sec_in_cluster, byte_in_sector)
}

/// optimize — Merge adjacent logical sectors into a single transfer if possible.
///
/// ASM: OPTIMIZE  86DOS.asm:2055-2123
///
/// Inputs:
///   dpb          — drive parameter block
///   start_sector — first logical sector
///   count        — number of sectors requested
/// Outputs:
///   Possibly-increased transfer count (sectors that are physically contiguous).
///
/// NOTE: differs from ASM because — in simulation we don't track physical
///       sector adjacency; we always return the requested count unchanged.
pub fn optimize(_dpb: &Dpb, _start: u16, count: u16) -> u16 {
    count
}

/// breakdown — Split a byte range into partial-first, whole-middle, and partial-last counts.
///
/// ASM: BREAKDOWN  86DOS.asm:1433-1463
///
/// Inputs:
///   byte_count  — total bytes to transfer (BX:CX in ASM)
///   byte_offset — byte offset within the first sector (DI in ASM)
///   secsiz      — sector size in bytes
/// Outputs:
///   (partial_first, whole_secs, partial_last)
///   partial_first — bytes to read/write in the partial first sector
///   whole_secs    — count of whole sectors in the middle
///   partial_last  — bytes to read/write in the partial last sector
///
/// The sum partial_first + whole_secs*secsiz + partial_last == byte_count.
pub fn breakdown(byte_count: u32, byte_offset: u16, secsiz: u16) -> (u16, u16, u16) {
    if byte_count == 0 {
        return (0, 0, 0);
    }
    let secsiz = secsiz as u32;
    // ASM line 1437: how many bytes remain in the first sector?
    let first_avail = secsiz - byte_offset as u32;
    let partial_first = if byte_count <= first_avail {
        byte_count as u16
    } else {
        first_avail as u16
    };
    let remaining = byte_count - partial_first as u32;
    // ASM line 1444: DIV secsiz → whole sectors + remainder
    let whole_secs = (remaining / secsiz) as u16;
    let partial_last = (remaining % secsiz) as u16;
    (partial_first, whole_secs, partial_last)
}
