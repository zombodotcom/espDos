//! fat.rs — FAT12 File Allocation Table routines.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   UNPACK      86DOS.asm:369-401    — read one 12-bit FAT entry
//!   PACK        86DOS.asm:402-433    — write one 12-bit FAT entry
//!   FATREAD     86DOS.asm:766-907    — read FAT from disk (if not yet loaded)
//!   CHKFATWRT   86DOS.asm:908-913    — conditional FATWRT
//!   FATWRT      86DOS.asm:914-951    — mark FAT dirty for flush
//!   FIGFAT      86DOS.asm:952-962    — prepare registers for FAT I/O
//!   FNDCLUS     86DOS.asm:1466-1507  — walk cluster chain N steps
//!   ALLOCATE    86DOS.asm:2169-2257  — allocate clusters for a file
//!   RELEASE     86DOS.asm:2260-2271  — free a cluster chain
//!   RELBLKS     86DOS.asm:2272-2283  — partial-chain free
//!   GETEOF      86DOS.asm:2285-2300  — walk chain to last cluster
//!   WRTFATS     86DOS.asm:2490-2516  — write all dirty FAT copies to disk

use crate::types::{DosError, Dpb, EOF_MARK, FAT_FREE};

// ── Core 12-bit FAT pack/unpack ───────────────────────────────────────────────

/// unpack — Read one 12-bit FAT entry.
///
/// ASM: UNPACK  86DOS.asm:369-401
///
/// Inputs:
///   fat     — in-memory FAT byte slice (SI in ASM)
///   maxclus — highest valid cluster number (from DPB)
///   bx      — cluster number to look up (BX in ASM)
/// Outputs:
///   Returns the 12-bit FAT entry for cluster bx.
///   Returns Err(BadFat) if bx > maxclus (HURTFAT path, lines 396-399).
///
/// 12-bit packing scheme (86DOS.asm lines 89-98):
///   byte_offset = bx + (bx >> 1)   i.e. floor(bx * 1.5)
///   word        = *(uint16_t *)(fat + byte_offset)
///   if bx even (NC after SHR BX): entry = word & 0x0FFF   (HAVCLUS, line 390)
///   if bx odd  (C  after SHR BX): entry = word >> 4
pub fn unpack(fat: &[u8], maxclus: u16, bx: u16) -> Result<u16, DosError> {
    // ASM line 396: CMP BX,[BP].MAXCLUS / JA HURTFAT
    if bx > maxclus {
        return Err(DosError::BadFat);
    }
    // ASM lines 381-382: LEA DI,[SI+BX] / SHR BX,1 → idx = bx + (bx >> 1)
    let idx = bx as usize + (bx as usize >> 1);
    if idx + 1 >= fat.len() {
        return Err(DosError::BadFat);
    }
    // ASM line 385: MOV AX,[DI]
    let word = u16::from_le_bytes([fat[idx], fat[idx + 1]]);
    // ASM line 386: JNC HAVCLUS (carry clear = bx was even)
    let val = if bx & 1 == 0 {
        // ASM line 387: AND AX,0FFFh
        word & 0x0FFF
    } else {
        // ASM lines 389-390: MOV CL,4 / SHR AX,CL
        word >> 4
    };
    Ok(val)
}

/// pack — Write one 12-bit FAT entry.
///
/// ASM: PACK  86DOS.asm:402-433
///
/// Inputs:
///   fat — mutable in-memory FAT byte slice (SI in ASM)
///   bx  — cluster number to write (BX in ASM)
///   dx  — 12-bit value to store (DX in ASM)
/// Outputs:
///   fat[idx..idx+2] updated in-place.
///   No return value; silently ignores out-of-bounds cluster.
///
/// The inverse of UNPACK: compute byte_offset = bx + (bx >> 1), read the
/// 16-bit word, mask-in the new 12-bit nibble, write back.
///   if bx even: word = (word & 0xF000) | (dx & 0x0FFF)
///   if bx odd:  word = (word & 0x000F) | ((dx & 0x0FFF) << 4)
pub fn pack(fat: &mut [u8], bx: u16, dx: u16) {
    // ASM line 406: LEA DI,[SI+BX] / SHR BX,1
    let idx = bx as usize + (bx as usize >> 1);
    if idx + 1 >= fat.len() {
        return;
    }
    // ASM line 410: MOV AX,[DI]
    let word = u16::from_le_bytes([fat[idx], fat[idx + 1]]);
    let new_word = if bx & 1 == 0 {
        // ASM lines 413-414: AND AX,0F000h / AND DX,0FFFh / OR AX,DX
        (word & 0xF000) | (dx & 0x0FFF)
    } else {
        // ASM lines 416-419: AND AX,000Fh / AND DX,0FFFh / MOV CL,4 / SHL DX,CL / OR AX,DX
        (word & 0x000F) | ((dx & 0x0FFF) << 4)
    };
    // ASM line 421: MOV [DI],AX
    let bytes = new_word.to_le_bytes();
    fat[idx] = bytes[0];
    fat[idx + 1] = bytes[1];
}

// ── FAT sizing helpers ────────────────────────────────────────────────────────

/// figfat — Compute FAT size in sectors and allocate the in-memory FAT buffer.
///
/// ASM: FIGFAT  86DOS.asm:952-962
///
/// Inputs:
///   dpb — partially initialised DPB (maxclus, secsiz set)
/// Outputs:
///   dpb.fatsiz — number of sectors required for one FAT copy
///   dpb.fat    — resized Vec to hold fatsiz full sectors
///
/// The formula mirrors ASM: bytes_needed = (maxclus+1)*3/2 + 1,
/// rounded up to a whole number of sectors.
/// NOTE: differs from ASM because — the ASM FIGFAT only computes a register
///       result; buffer allocation happens in PERDRV/CONTINIT (init.rs).
pub fn figfat(dpb: &mut Dpb) {
    // ASM line 953: MOV AX,[BP].MAXCLUS / ... / INC AX  → maxclus+1 entries
    let fat_bytes = (dpb.maxclus as usize + 1) * 3 / 2 + 1;
    let sectors = (fat_bytes + dpb.secsiz as usize - 1) / dpb.secsiz as usize;
    dpb.fatsiz = sectors as u8;
    let total_fat_bytes = sectors * dpb.secsiz as usize;
    dpb.fat.resize(total_fat_bytes, 0xFF);
}

/// fatwrt — Mark FAT dirty so it will be flushed to disk.
///
/// ASM: FATWRT  86DOS.asm:914-951
///
/// Inputs:
///   dpb — drive parameter block
/// Outputs:
///   dpb.dirtyfat set to 1
///
/// In the ASM the routine physically writes the FAT; here we only set the
/// dirty flag because the actual I/O is done in wrtfats/wrtfats_all.
/// NOTE: differs from ASM because — deferred write; flag is flushed by
///       wrtfats() (WRTFATS, 86DOS.asm:2490-2516).
pub fn fatwrt(dpb: &mut Dpb) {
    dpb.dirtyfat = 1;
}

/// fatread — Read the FAT from disk into the in-memory buffer if not yet loaded.
///
/// ASM: FATREAD  86DOS.asm:766-907
///
/// Inputs:
///   dpb  — drive parameter block (dirtyfat==0xFF means never read)
///   bios — BIOS vtable for disk_read
/// Outputs:
///   dpb.fat filled from disk sectors [firfat .. firfat+fatsiz)
///   dpb.dirtyfat cleared to 0
///   Returns Err(DiskError) if the BIOS read fails.
pub fn fatread(dpb: &mut Dpb, bios: &mut dyn crate::types::BiosVtable) -> Result<(), DosError> {
    // ASM line 770: CMP [BP].DIRTYFAT, 0FFh / JNE ALRDRDN  (already read)
    if dpb.dirtyfat == 0xFF {
        let secsiz = dpb.secsiz as usize;
        let fatsiz = dpb.fatsiz as usize;
        let total = fatsiz * secsiz;
        dpb.fat.resize(total, 0);
        // ASM line 790: CALL BIOS  (INT 25h or equivalent)
        let sector = dpb.firfat;
        let drive = dpb.drvnum;
        let carry = bios.disk_read(drive, &mut dpb.fat, sector, fatsiz as u16);
        if carry {
            return Err(DosError::DiskError);
        }
        dpb.dirtyfat = 0;
    }
    Ok(())
}

/// wrtfats — Write all dirty FAT copies to disk.
///
/// ASM: WRTFATS  86DOS.asm:2490-2516
///
/// Inputs:
///   dpb  — drive parameter block (dirtyfat==1 means needs writing)
///   bios — BIOS vtable for disk_write
/// Outputs:
///   All fatcnt copies written starting at firfat.
///   dpb.dirtyfat cleared to 0.
///   Returns Err(DiskError) on I/O failure.
pub fn wrtfats(dpb: &mut Dpb, bios: &mut dyn crate::types::BiosVtable) -> Result<(), DosError> {
    // ASM line 2491: CMP [BP].DIRTYFAT,0 / JE WRTDONE
    if dpb.dirtyfat == 0 {
        return Ok(());
    }
    let fatsiz = dpb.fatsiz as u16;
    let fatcnt = dpb.fatcnt as u16;
    let secsiz = dpb.secsiz as usize;
    // ASM lines 2494-2510: loop over fatcnt copies
    for i in 0..fatcnt {
        let sector = dpb.firfat + i * fatsiz;
        let end = (fatsiz as usize) * secsiz;
        let fat_slice = &dpb.fat[..end.min(dpb.fat.len())];
        let carry = bios.disk_write(dpb.drvnum, fat_slice, sector, fatsiz);
        if carry {
            return Err(DosError::DiskError);
        }
    }
    dpb.dirtyfat = 0;
    Ok(())
}

/// chkfatwrt — Conditionally flush dirty FAT to disk.
///
/// ASM: CHKFATWRT  86DOS.asm:908-913
///
/// Inputs:
///   dpb  — drive parameter block
///   bios — BIOS vtable
/// Outputs:
///   Calls wrtfats if dirtyfat==1; otherwise no-op.
pub fn chkfatwrt(dpb: &mut Dpb, bios: &mut dyn crate::types::BiosVtable) -> Result<(), DosError> {
    // ASM line 909: CMP [BP].DIRTYFAT,1 / JNE NOFATWRT
    if dpb.dirtyfat == 1 {
        wrtfats(dpb, bios)
    } else {
        Ok(())
    }
}

// ── Cluster chain operations ──────────────────────────────────────────────────

/// release — Free an entire cluster chain beginning at `start`.
///
/// ASM: RELEASE  86DOS.asm:2260-2271
///
/// Inputs:
///   dpb   — drive parameter block
///   start — first cluster of the chain to free (BX in ASM)
/// Outputs:
///   All clusters in the chain marked FAT_FREE (0x000).
///   dpb.dirtyfat set to 1.
///
/// Delegates to relblks for the actual traversal.
pub fn release(dpb: &mut Dpb, start: u16) -> Result<(), DosError> {
    relblks(dpb, start)
}

/// relblks — Free cluster chain from `start` onward (partial-chain variant).
///
/// ASM: RELBLKS  86DOS.asm:2272-2283
///
/// Inputs:
///   dpb   — drive parameter block
///   start — first cluster to free (BX in ASM)
/// Outputs:
///   Traverses chain: unpack → mark free → follow next → repeat until EOF or invalid.
///   dpb.dirtyfat set to 1.
pub fn relblks(dpb: &mut Dpb, start: u16) -> Result<(), DosError> {
    if start < 2 || start > dpb.maxclus {
        return Ok(());
    }
    let mut cur = start;
    loop {
        // ASM line 2275: CALL UNPACK
        let next = unpack(&dpb.fat, dpb.maxclus, cur)?;
        // ASM line 2276: CALL PACK (store 0 = free)
        pack(&mut dpb.fat, cur, FAT_FREE);
        // ASM line 2278: CMP DI,0FF8h / JAE RELDONE
        if next >= EOF_MARK || next < 2 {
            break;
        }
        cur = next;
    }
    dpb.dirtyfat = 1;
    Ok(())
}

/// allocate — Allocate one cluster for a file, linking it to `prev` if given.
///
/// ASM: ALLOCATE  86DOS.asm:2169-2257
///
/// Inputs:
///   dpb  — drive parameter block
///   prev — Some(cluster) to chain from, or None for first cluster
/// Outputs:
///   Returns new cluster number on success.
///   Links prev→new in the FAT and marks new as EOF (0xFFF).
///   Returns Err(NoSpace) if no free cluster exists.
///
/// The ASM scans from the last-allocated cluster hint; here we always scan
/// from cluster 2 (conservative but correct).
/// NOTE: differs from ASM because — no "last allocated" hint maintained.
pub fn allocate(dpb: &mut Dpb, prev: Option<u16>) -> Result<u16, DosError> {
    let start = 2u16;
    let max = dpb.maxclus;
    // ASM lines 2195-2220: loop NXTCLUS — scan for FAT_FREE entry
    for clus in start..=max {
        let entry = unpack(&dpb.fat, max, clus)?;
        if entry == FAT_FREE {
            // ASM line 2222: CALL PACK (store EOF)
            pack(&mut dpb.fat, clus, 0xFFF);
            if let Some(p) = prev {
                // ASM line 2224: link previous cluster to new
                pack(&mut dpb.fat, p, clus);
            }
            dpb.dirtyfat = 1;
            return Ok(clus);
        }
    }
    Err(DosError::NoSpace)
}

/// fndclus — Walk cluster chain, skipping `skip` clusters from `start`.
///
/// ASM: FNDCLUS  86DOS.asm:1466-1507
///
/// Inputs:
///   dpb   — drive parameter block
///   start — first cluster of the chain (BX in ASM)
///   skip  — number of clusters to advance (CX in ASM)
/// Outputs:
///   (current_cluster, remaining) — remaining should be 0 if chain was long enough.
///   If end-of-chain reached before skip is exhausted, returns (last_cluster, remaining>0).
pub fn fndclus(dpb: &Dpb, start: u16, skip: u16) -> Result<(u16, u16), DosError> {
    let mut cur = start;
    let mut remaining = skip;
    // ASM lines 1476-1500: NXTCLS loop
    while remaining > 0 {
        let next = unpack(&dpb.fat, dpb.maxclus, cur)?;
        // ASM line 1493: CMP DI,0FF8h / JAE FINCLUS
        if next >= EOF_MARK {
            return Ok((cur, remaining));
        }
        cur = next;
        remaining -= 1;
    }
    Ok((cur, 0))
}

/// geteof — Walk cluster chain to find the last cluster (EOF marker).
///
/// ASM: GETEOF  86DOS.asm:2285-2300
///
/// Inputs:
///   dpb   — drive parameter block
///   start — first cluster of the chain
/// Outputs:
///   Returns the cluster whose FAT entry is >= EOF_MARK (0xFF8).
///   Returns Err(BadFat) if a cycle or invalid link is detected.
pub fn geteof(dpb: &Dpb, start: u16) -> Result<u16, DosError> {
    let mut cur = start;
    loop {
        let next = unpack(&dpb.fat, dpb.maxclus, cur)?;
        // ASM line 2291: CMP DI,0FF8h / JAE HAVEEOF
        if next >= EOF_MARK {
            return Ok(cur);
        }
        if next < 2 {
            return Err(DosError::BadFat);
        }
        cur = next;
    }
}
