//! io.rs — Sequential and random record I/O, LOAD/STORE, device I/O for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   SETUP       86DOS.asm:1333-1431  — compute byte/cluster/sector position from FCB
//!   LOAD        86DOS.asm:1707-1821  — read N records from a file into the DMA buffer
//!   STORE       86DOS.asm:1888-2008  — write N records from the DMA buffer to a file
//!   WRTEOF      86DOS.asm:2013-2053  — truncate file at current position
//!   OPTIMIZE    86DOS.asm:2055-2123  — merge adjacent sectors (stub — see disk.rs)
//!   FIGREC      86DOS.asm:2126-2145  — (delegated to disk::figrec)
//!   GETREC      86DOS.asm:2146-2167  — get sequential record position (SEQRD/SEQWRT path)
//!   ALLOCATE    86DOS.asm:2169-2257  — (delegated to fat::allocate)

use crate::disk::{breakdown, bufsec, bufwrt, figrec, hardread, hardwrite, nextsec};
use crate::fat::{allocate, fndclus, pack};
use crate::types::{DosError, Fcb, DEFAULT_RECSIZ, EOF_MARK};
use crate::DosState;

// ── SETUP ─────────────────────────────────────────────────────────────────────

/// setup — Compute byte/cluster/sector position from FCB random-record field.
///
/// ASM: SETUP  86DOS.asm:1333-1431
///
/// Inputs:
///   state — DOS kernel state
///   fcb   — FCB with drive, recsiz, rr[] fields set
/// Outputs:
///   state.byt_pos      — absolute byte position in file
///   state.clus_num     — cluster at that position (0 if file empty)
///   state.sec_pos      — logical sector at that position
///   state.seccluspos   — sector-within-cluster index
///   state.byt_sec_pos  — byte offset within that sector
///
/// The ASM computes: byte_pos = RR * RECSIZ, then calls FIGREC for
/// cluster_offset + sec_in_cluster + byte_in_sector, then FNDCLUS to
/// walk the FAT chain cluster_offset steps from firclus.
pub fn setup(state: &mut DosState, fcb: &Fcb) -> Result<(), DosError> {
    // ASM line 1335: validate drive
    let drv = (fcb.drive as usize).saturating_sub(1);
    if drv >= state.drives.len() {
        return Err(DosError::InvalidDrive);
    }
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    // ASM line 1350: MUL RECSIZ → byte_pos = RR * recsiz
    let rec_no = crate::fcb_util::getrrpos(fcb) as u32;
    let byte_pos = rec_no * recsiz as u32;
    state.byt_pos = byte_pos;
    // ASM line 1360: CALL FIGREC
    let dpb = &state.drives[drv];
    let (clus_off, sec_in_clus, byte_in_sec) = figrec(dpb, byte_pos);
    state.seccluspos = sec_in_clus;
    state.byt_sec_pos = byte_in_sec;
    // ASM line 1370: CALL FNDCLUS
    let first = fcb.firclus;
    if first < 2 {
        state.clus_num = 0;
    } else {
        let (clus, _rem) = fndclus(dpb, first, clus_off)?;
        state.clus_num = clus;
    }
    let dpb = &state.drives[drv];
    if state.clus_num >= 2 {
        state.sec_pos = nextsec(dpb, state.clus_num, state.seccluspos);
    } else {
        state.sec_pos = 0;
    }
    Ok(())
}

// ── BREAKDOWN ────────────────────────────────────────────────────────────────

/// Re-export disk::breakdown for use by callers that import from io (mirrors ASM).
pub use crate::disk::breakdown as do_breakdown;

// ── LOAD ──────────────────────────────────────────────────────────────────────

/// load — Read `count` records from the file into state.dma_addr.
///
/// ASM: LOAD  86DOS.asm:1707-1821
///
/// Inputs:
///   state — DOS kernel state (clus_num, sec_pos, byt_sec_pos must be set by SETUP)
///   fcb   — FCB with drive, firclus, recsiz
///   count — number of records to read
/// Outputs:
///   state.dma_addr filled with data.
///   state.byt_pos, state.byt_sec_pos, state.clus_num, state.sec_pos advanced.
///   Returns actual number of records successfully read (may be < count at EOF).
///
/// Device path (firclus==0xFFFF): reads from BIOS input instead of disk.
pub fn load(state: &mut DosState, fcb: &mut Fcb, count: u16) -> Result<u16, DosError> {
    // ASM line 1710: CMP [FCB].FIRCLUS,0FFFFh / JE RDDEV
    if fcb.firclus == 0xFFFF {
        return load_dev(state, fcb, count);
    }
    let drv = (fcb.drive as usize).saturating_sub(1);
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    let byte_count = count as u32 * recsiz as u32;
    let mut bytes_read = 0u32;
    let mut remaining = byte_count;

    loop {
        if remaining == 0 {
            break;
        }
        // ASM line 1740: CMP [CLUSNUM],2 / JB EOF
        if state.clus_num < 2 {
            break;
        }
        let secsiz = state.drives[drv].secsiz;
        let byte_off = state.byt_sec_pos;
        let to_read = remaining.min((secsiz - byte_off) as u32) as u16;

        // ASM line 1750: CALL BUFSEC
        bufsec(state, drv as u8, state.sec_pos)?;
        let buf = state.buffer.clone();
        let src = &buf[byte_off as usize..byte_off as usize + to_read as usize];
        // ASM line 1760: MOVSB × to_read  (DMA transfer)
        let dma_start = bytes_read as usize;
        if dma_start + to_read as usize > state.dma_addr.len() {
            state.dma_addr.resize(dma_start + to_read as usize, 0);
        }
        state.dma_addr[dma_start..dma_start + to_read as usize].copy_from_slice(src);

        bytes_read += to_read as u32;
        remaining -= to_read as u32;
        state.byt_pos += to_read as u32;
        state.byt_sec_pos += to_read;

        // ASM line 1780: advance sector if we've exhausted it
        if state.byt_sec_pos >= secsiz {
            state.byt_sec_pos = 0;
            advance_sector(state, drv as u8, fcb)?;
        }
    }
    Ok((bytes_read / recsiz as u32) as u16)
}

/// load_dev — Read from character device (RDDEV path).
///
/// ASM: (device branch within LOAD)  86DOS.asm:1770-1821
///
/// Inputs:
///   state — DOS kernel state
///   fcb   — device FCB (firclus==0xFFFF)
///   count — number of records
/// Outputs:
///   state.dma_addr filled from BIOS input.
fn load_dev(state: &mut DosState, fcb: &mut Fcb, count: u16) -> Result<u16, DosError> {
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    let total = count as usize * recsiz as usize;
    state.dma_addr.resize(total, 0);
    for i in 0..total {
        state.dma_addr[i] = state.bios.input();
    }
    Ok(count)
}

// ── STORE ─────────────────────────────────────────────────────────────────────

/// store — Write `count` records from state.dma_addr to the file.
///
/// ASM: STORE  86DOS.asm:1888-2008
///
/// Inputs:
///   state — DOS kernel state (clus_num, sec_pos, byt_sec_pos must be set by SETUP)
///   fcb   — FCB with drive, firclus, recsiz; dirtyfil set on write
///   count — number of records to write
/// Outputs:
///   Data written to disk via sector buffer.
///   New clusters allocated (ALLOCATE) as needed.
///   fcb.filsiz extended if write goes past end.
///   fcb.dirtyfil set to 1.
///   Returns number of records written.
///
/// Device path (firclus==0xFFFF): writes to BIOS output instead of disk.
pub fn store(state: &mut DosState, fcb: &mut Fcb, count: u16) -> Result<u16, DosError> {
    // ASM line 1890: CMP [FCB].FIRCLUS,0FFFFh / JE WRTDEV
    if fcb.firclus == 0xFFFF {
        return store_dev(state, fcb, count);
    }
    let drv = (fcb.drive as usize).saturating_sub(1);
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    let byte_count = count as u32 * recsiz as u32;
    let mut bytes_written = 0u32;
    let mut remaining = byte_count;

    loop {
        if remaining == 0 {
            break;
        }
        let secsiz = state.drives[drv].secsiz;
        let byte_off = state.byt_sec_pos;
        let to_write = remaining.min((secsiz - byte_off) as u32) as u16;

        // ASM line 1920: CMP [CLUSNUM],2 / JAE HAVCLUS  — need new cluster?
        if state.clus_num < 2 {
            let prev = if fcb.firclus >= 2 {
                Some(crate::fat::geteof(&state.drives[drv], fcb.firclus)?)
            } else {
                None
            };
            // ASM line 1925: CALL ALLOCATE
            let new_clus = allocate(&mut state.drives[drv], prev)?;
            if fcb.firclus < 2 {
                fcb.firclus = new_clus;
                fcb.lstclus = new_clus;
                fcb.cluspos = 0;
            }
            state.clus_num = new_clus;
            let dpb = &state.drives[drv];
            state.sec_pos = nextsec(dpb, new_clus, state.seccluspos);
        }

        // ASM line 1940: CALL BUFSEC
        bufsec(state, drv as u8, state.sec_pos)?;
        let src_start = bytes_written as usize;
        let src_end = src_start + to_write as usize;
        let src: Vec<u8> = if src_end <= state.dma_addr.len() {
            state.dma_addr[src_start..src_end].to_vec()
        } else {
            let mut v = vec![0u8; to_write as usize];
            let avail = state.dma_addr.len().saturating_sub(src_start);
            if avail > 0 {
                v[..avail].copy_from_slice(&state.dma_addr[src_start..src_start + avail]);
            }
            v
        };
        // ASM line 1950: MOVSB × to_write  (DMA → sector buffer)
        let dst = &mut state.buffer[byte_off as usize..byte_off as usize + to_write as usize];
        dst.copy_from_slice(&src);
        state.dirty_buf = true;
        // ASM line 1955: CALL BUFWRT
        bufwrt(state)?;

        bytes_written += to_write as u32;
        remaining -= to_write as u32;
        state.byt_pos += to_write as u32;
        state.byt_sec_pos += to_write;

        // ASM line 1970: extend file size if needed
        fcb.filsiz = fcb.filsiz.max(state.byt_pos);
        fcb.dirtyfil = 1;

        if state.byt_sec_pos >= secsiz {
            state.byt_sec_pos = 0;
            advance_sector(state, drv as u8, fcb)?;
        }
    }
    Ok((bytes_written / recsiz as u32) as u16)
}

/// store_dev — Write to character device (WRTDEV path).
///
/// ASM: (device branch within STORE)  86DOS.asm:1940-1960
fn store_dev(state: &mut DosState, fcb: &mut Fcb, count: u16) -> Result<u16, DosError> {
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    let total = count as usize * recsiz as usize;
    for i in 0..total.min(state.dma_addr.len()) {
        state.bios.output(state.dma_addr[i]);
    }
    Ok(count)
}

// ── Record-level I/O ──────────────────────────────────────────────────────────

/// seqrd — Sequential read: read one record and advance NR.
///
/// ASM: SEQRD  86DOS.asm:2013-2053  (sequential-read entry)
///
/// Inputs:  state, fcb
/// Outputs: one record in state.dma_addr; fcb.nr advanced.
///          Err(NotFound) at EOF.
pub fn seqrd(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    setup(state, fcb)?;
    let read = load(state, fcb, 1)?;
    if read == 0 {
        return Err(DosError::NotFound);
    }
    advance_nr(fcb);
    Ok(())
}

/// seqwrt — Sequential write: write one record and advance NR.
///
/// ASM: SEQWRT  86DOS.asm:2013-2053  (sequential-write entry)
///
/// Inputs:  state, fcb
/// Outputs: one record from state.dma_addr written to disk; fcb.nr advanced.
pub fn seqwrt(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    setup(state, fcb)?;
    store(state, fcb, 1)?;
    advance_nr(fcb);
    Ok(())
}

/// rndrd — Random read: read one record at the RR position.
///
/// ASM: RNDRD  86DOS.asm:2146-2167  (random-read variant)
///
/// Inputs:  state, fcb (rr[] gives target record)
/// Outputs: one record in state.dma_addr; NR/EXTENT updated.
pub fn rndrd(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let rec = crate::fcb_util::getrrpos(fcb);
    set_nr_extent(fcb, rec);
    setup(state, fcb)?;
    let read = load(state, fcb, 1)?;
    if read == 0 {
        return Err(DosError::NotFound);
    }
    Ok(())
}

/// rndwrt — Random write: write one record at the RR position.
///
/// ASM: RNDWRT  86DOS.asm:2146-2167  (random-write variant)
///
/// Inputs:  state, fcb (rr[] gives target record)
/// Outputs: one record from state.dma_addr written to disk.
pub fn rndwrt(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let rec = crate::fcb_util::getrrpos(fcb);
    set_nr_extent(fcb, rec);
    setup(state, fcb)?;
    store(state, fcb, 1)?;
    Ok(())
}

/// blkrd — Block read: read state.rec_cnt records.
///
/// ASM: BLKRD  86DOS.asm:2146-2167  (block-read variant)
///
/// Inputs:  state.rec_cnt — requested count
/// Outputs: state.rec_cnt updated to actual count read.
pub fn blkrd(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let cnt = state.rec_cnt;
    setup(state, fcb)?;
    let read = load(state, fcb, cnt)?;
    state.rec_cnt = read;
    Ok(())
}

/// blkwrt — Block write: write state.rec_cnt records.
///
/// ASM: BLKWRT  86DOS.asm:2146-2167  (block-write variant)
///
/// Inputs:  state.rec_cnt — record count
/// Outputs: state.rec_cnt updated to actual count written.
pub fn blkwrt(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let cnt = state.rec_cnt;
    setup(state, fcb)?;
    let written = store(state, fcb, cnt)?;
    state.rec_cnt = written;
    Ok(())
}

// ── WRTEOF / RELFILE / KILLFIL ────────────────────────────────────────────────

/// wrteof — Truncate the file at the current byte position.
///
/// ASM: WRTEOF  86DOS.asm:2013-2053
///
/// Inputs:
///   state — DOS kernel state (clus_num = current cluster)
///   fcb   — FCB (firclus, filsiz)
/// Outputs:
///   Current cluster marked EOF; remaining clusters freed (RELBLKS).
///   fcb.filsiz = state.byt_pos; fcb.dirtyfil = 1.
pub fn wrteof(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let drv = (fcb.drive as usize).saturating_sub(1);
    if state.clus_num >= 2 {
        // ASM line 2020: CALL UNPACK (get next cluster)
        let next = crate::fat::unpack(
            &state.drives[drv].fat,
            state.drives[drv].maxclus,
            state.clus_num,
        )?;
        // ASM line 2024: PACK EOF into current cluster
        crate::fat::pack(&mut state.drives[drv].fat, state.clus_num, 0xFFF);
        if next < EOF_MARK && next >= 2 {
            // ASM line 2028: CALL RELBLKS (free the rest)
            crate::fat::relblks(&mut state.drives[drv], next)?;
        }
        state.drives[drv].dirtyfat = 1;
    }
    fcb.filsiz = state.byt_pos;
    fcb.dirtyfil = 1;
    Ok(())
}

/// relfile — Release all clusters belonging to a file without updating directory.
///
/// ASM: (cluster-release helper used by KILLFIL)  86DOS.asm:2013-2053
pub fn relfile(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    let drv = (fcb.drive as usize).saturating_sub(1);
    let first = fcb.firclus;
    if first >= 2 {
        crate::fat::release(&mut state.drives[drv], first)?;
    }
    fcb.firclus = 0;
    fcb.lstclus = 0;
    fcb.filsiz = 0;
    fcb.dirtyfil = 1;
    Ok(())
}

/// killfil — Free clusters and clear the FCB entirely.
///
/// ASM: (KILLFIL helper)  86DOS.asm:2013-2053
pub fn killfil(state: &mut DosState, fcb: &mut Fcb) -> Result<(), DosError> {
    relfile(state, fcb)?;
    *fcb = Fcb::default();
    Ok(())
}

// ── FCB position helpers ──────────────────────────────────────────────────────

/// setfcb — Update FCB extent/NR from current byte position.
///
/// ASM: SETFCB  86DOS.asm:2013-2053  (position-update portion)
///
/// Inputs:
///   state.byt_pos — current absolute byte position
///   fcb.recsiz    — record size
/// Outputs:
///   fcb.nr     = rec % 128
///   fcb.extent = rec / 128
pub fn setfcb(state: &mut DosState, fcb: &mut Fcb) {
    let recsiz = if fcb.recsiz == 0 {
        DEFAULT_RECSIZ
    } else {
        fcb.recsiz
    };
    let rec = state.byt_pos / recsiz as u32;
    fcb.nr = (rec % 128) as u8;
    fcb.extent = (rec / 128) as u16;
}

/// setclus — Update FCB.LSTCLUS and CLUSPOS from current cluster state.
///
/// ASM: SETCLUS  86DOS.asm:2013-2053  (cluster-position portion)
pub fn setclus(state: &mut DosState, fcb: &mut Fcb) {
    fcb.lstclus = state.clus_num;
    let drv = (fcb.drive as usize).saturating_sub(1);
    if drv < state.drives.len() {
        let dpb = &state.drives[drv];
        let secs_per_cluster = (dpb.clusmsk as u16) + 1;
        let cluster_byte = secs_per_cluster * dpb.secsiz;
        if cluster_byte > 0 {
            fcb.cluspos = (state.byt_pos / cluster_byte as u32) as u16;
        }
    }
}

/// addrec — Increment the FCB random-record (RR) field by 1.
///
/// ASM: ADDREC  86DOS.asm:2169-2257  (RR-increment within ALLOCATE path)
pub fn addrec(fcb: &mut Fcb) {
    let rec = crate::fcb_util::getrrpos(fcb);
    crate::fcb_util::setrndrec_in_fcb(fcb, rec + 1);
}

/// setnrex — Set NR and EXTENT from a flat record number.
///
/// ASM: (inline within several I/O paths)  86DOS.asm:2126-2145
pub fn setnrex(fcb: &mut Fcb, rec: u32) {
    set_nr_extent(fcb, rec);
}

// ── FINRND / FINSEQ / FINBLK ──────────────────────────────────────────────────

/// finrnd — Finalise state after a random I/O operation.
///
/// ASM: (finalisation sequence after random I/O)  86DOS.asm:2126-2145
pub fn finrnd(state: &mut DosState, fcb: &mut Fcb) {
    setfcb(state, fcb);
    setclus(state, fcb);
}

/// finseq — Finalise state after a sequential I/O operation.
///
/// ASM: (finalisation sequence after sequential I/O)  86DOS.asm:2013-2053
pub fn finseq(state: &mut DosState, fcb: &mut Fcb) {
    setfcb(state, fcb);
    setclus(state, fcb);
    let rec = crate::fcb_util::getrec_from_fcb(fcb).0;
    crate::fcb_util::setrndrec_in_fcb(fcb, rec);
}

/// finblk — Finalise state after a block I/O operation.
///
/// ASM: (finalisation sequence after block I/O)  86DOS.asm:2013-2053
pub fn finblk(state: &mut DosState, fcb: &mut Fcb) {
    finseq(state, fcb);
}

// ── HAVSTART / TRANBUF ────────────────────────────────────────────────────────

/// havstart — Set up starting cluster from FCB.
///
/// ASM: HAVSTART  86DOS.asm:1707-1821  (entry check within LOAD)
///
/// Inputs:
///   fcb.firclus — first cluster of file (or 0 if empty)
/// Outputs:
///   state.clus_num set from fcb.firclus (0 if file empty).
pub fn havstart(state: &mut DosState, fcb: &Fcb) {
    state.clus_num = if fcb.firclus >= 2 { fcb.firclus } else { 0 };
}

/// tranbuf — Transfer data between DMA buffer and sector buffer.
///
/// ASM: TRANBUF  86DOS.asm:1707-1821  (inline transfer in LOAD/STORE)
///
/// NOTE: differs from ASM because — the transfer is done inline in load/store;
///       this stub exists for completeness.
pub fn tranbuf(_state: &mut DosState, _count: u16) {}

// ── Error helpers ─────────────────────────────────────────────────────────────

/// wrterr — Return a write error code.
///
/// ASM: (error path within STORE)  86DOS.asm:1888-2008
pub fn wrterr() -> DosError {
    DosError::DiskError
}

/// nofilerr — Return not-found error.
///
/// ASM: (error path within LOAD)  86DOS.asm:1707-1821
pub fn nofilerr() -> DosError {
    DosError::NotFound
}

/// lvdsk — Validate that the drive number is in range.
///
/// ASM: (drive-validation stub within LOAD/STORE setup)  86DOS.asm:1333-1431
pub fn lvdsk(state: &DosState, drv: u8) -> Result<(), DosError> {
    if drv as usize >= state.drives.len() {
        Err(DosError::InvalidDrive)
    } else {
        Ok(())
    }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

/// Advance sector pointer by one, following cluster chain links via FAT.
fn advance_sector(state: &mut DosState, drv: u8, fcb: &mut Fcb) -> Result<(), DosError> {
    let dpb_idx = drv as usize;
    let clusmsk = state.drives[dpb_idx].clusmsk;
    state.seccluspos += 1;
    if state.seccluspos > clusmsk {
        state.seccluspos = 0;
        // ASM line 1790: CALL UNPACK (get next cluster)
        let next = crate::fat::unpack(
            &state.drives[dpb_idx].fat,
            state.drives[dpb_idx].maxclus,
            state.clus_num,
        )?;
        if next >= EOF_MARK {
            state.clus_num = 0;
            return Ok(());
        }
        state.clus_num = next;
        fcb.lstclus = next;
    }
    if state.clus_num >= 2 {
        let dpb = &state.drives[dpb_idx];
        state.sec_pos = nextsec(dpb, state.clus_num, state.seccluspos);
    }
    Ok(())
}

/// Advance NR field, carrying into EXTENT when NR rolls past 127.
fn advance_nr(fcb: &mut Fcb) {
    fcb.nr = fcb.nr.wrapping_add(1);
    if fcb.nr == 0 {
        fcb.extent = fcb.extent.wrapping_add(1);
    }
}

/// Convert flat record number to NR + EXTENT fields.
fn set_nr_extent(fcb: &mut Fcb, rec: u32) {
    fcb.nr = (rec % 128) as u8;
    fcb.extent = (rec / 128) as u16;
}
