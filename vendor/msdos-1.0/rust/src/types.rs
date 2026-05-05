//! types.rs — Constants, structs, and enums for the 86-DOS 1.00 Rust translation.
//!
//! Translated from 86DOS.asm.  The following ASM data definitions are covered here:
//!
//!   MAXCALL / MAXCOM equates  86DOS.asm:~206-215   — function-number limits
//!   ESCCH / INTBASE / INTTAB  86DOS.asm:~60-100    — interrupt/keyboard equates
//!   BIOSSEG / EOF_MARK        86DOS.asm:~100-120   — BIOS segment, FAT sentinel
//!   DPB layout                86DOS.asm:~3306-3423 — per-drive parameter block
//!   FCB layout                86DOS.asm:~3110-3145 — file control block
//!   DMAADD / ENDMEM globals   86DOS.asm:3228-3240  — global data area equates

// ── Constants (from ASM equates) ────────────────────────────────────────────

/// MAXCALL — highest function number accepted via ENTRY (CL path).
/// ASM: CMP CL,MAXCALL  86DOS.asm:213
pub const MAXCALL: u8 = 36;

/// MAXCOM — highest function number accepted via COMMAND (INT 20h / CALL 0).
/// ASM: CMP AH,MAXCOM  86DOS.asm:200
pub const MAXCOM: u8 = 41;

/// ESCCH — ASCII escape character used in console template editing.
/// ASM: ESCCH EQU 1BH  86DOS.asm:~85
pub const ESCCH: u8 = 0x1B;

/// INTBASE — base interrupt vector for DOS (INT 20h–3Fh mapped here).
/// ASM: INTBASE EQU 80H  86DOS.asm:~60
pub const INTBASE: u16 = 0x80;

/// INTTAB — base of user interrupt table (segment 0, offset 0x20×4).
/// ASM: INTTAB EQU 20H  86DOS.asm:~62
pub const INTTAB: u16 = 0x20;

/// ENTRYPOINTSEG — CS-relative segment of the CALL 5 entry point.
/// ASM: ENTRYPOINTSEG EQU 0CH  86DOS.asm:49
pub const ENTRYPOINTSEG: u16 = 0x0C;

/// ENTRYPOINT — offset of the CALL 5 / INT 21h entry point.
/// ASM: ENTRYPOINT EQU INTBASE+40H  86DOS.asm:50
pub const ENTRYPOINT: u16 = INTBASE + 0x40;

/// CONTC — interrupt vector offset for Ctrl-C handler.
/// ASM: CONTC EQU INTTAB+3  86DOS.asm:~65
pub const CONTC: u16 = INTTAB + 3;

/// EXIT_VEC — offset of the program exit vector in the interrupt table.
/// ASM: EXIT EQU INTBASE+8  86DOS.asm:~68
pub const EXIT_VEC: u16 = INTBASE + 8;

/// LONGJUMP — opcode byte for a far (long) jump instruction (JMP FAR).
/// ASM: LONGJUMP EQU 0EAH  86DOS.asm:~70
pub const LONGJUMP: u8 = 0xEA;

/// LONGCALL — opcode byte for a far (long) call instruction (CALL FAR).
/// ASM: LONGCALL EQU 9AH  86DOS.asm:~72
pub const LONGCALL: u8 = 0x9A;

/// MAXDIF — maximum FAT entry value that is still a valid cluster pointer.
/// Entries >= 0xFF8 are end-of-chain marks; MAXDIF = 0x0FFF is the mask.
/// ASM: MAXDIF EQU 0FFFH  86DOS.asm:~80
pub const MAXDIF: u16 = 0x0FFF;

/// SAVEXIT — offset within the FCB area where the original exit vector is saved.
/// ASM: SAVEXIT EQU 10  86DOS.asm:~82
pub const SAVEXIT: u16 = 10;

/// BIOSSEG — segment address of the ROM BIOS data area / BIOS entry points.
/// ASM: BIOSSEG EQU 40H  86DOS.asm:~84
pub const BIOSSEG: u16 = 0x40;

/// EOF_MARK — FAT12 end-of-chain sentinel (any value >= 0xFF8 means EOF).
/// ASM: used in UNPACK/PACK comparisons  86DOS.asm:~390-395
pub const EOF_MARK: u16 = 0xFF8;

/// DEL_MARK — first byte of a deleted directory entry.
/// ASM: DB 0E5H used as FREEDIRBLK sentinel  86DOS.asm:~630
pub const DEL_MARK: u8 = 0xE5;

/// SMALLDIR_ENTRY — size in bytes of a SMALLDIR=1 directory entry (16 bytes).
pub const SMALLDIR_ENTRY: usize = 16;

/// LARGE_ENTRY — size in bytes of a standard 32-byte directory entry.
pub const LARGE_ENTRY: usize = 32;

/// DEFAULT_RECSIZ — default FCB record size (128 bytes).
/// ASM: RECSIZ field initialised to 128 when FCB is opened  86DOS.asm:~730
pub const DEFAULT_RECSIZ: u16 = 128;

/// FAT_FREE — FAT12 value for a free (unallocated) cluster.
/// ASM: cluster entry of 0 means free  86DOS.asm:~395
pub const FAT_FREE: u16 = 0x000;

// ── DosError ─────────────────────────────────────────────────────────────────

/// DosError — error codes returned by DOS kernel routines.
///
/// These correspond to the carry-set error paths in 86DOS.asm.
/// The ASM typically sets AL to an error code or jumps to ERROR/BADCALL;
/// here we use a typed enum instead.
#[derive(Debug, Clone, PartialEq)]
pub enum DosError {
    /// File or directory not found (NONE path, e.g. 86DOS.asm:596-601)
    NotFound,
    /// Drive number out of range (SELDSK / GETDSKPT bounds check)
    InvalidDrive,
    /// BIOS disk read/write returned carry set (HARDERR, 86DOS.asm:1186-1214)
    DiskError,
    /// No free clusters available (ALLOCATE, 86DOS.asm:2169-2257)
    NoSpace,
    /// Bad filename or parse error (MAKEFCB, LODNAME)
    BadFileName,
    /// FAT entry out of range / corrupt (HURTFAT, 86DOS.asm:396-399)
    BadFat,
    /// All FAT copies are bad (BADFATMES path, 86DOS.asm:~3213)
    AllFatsBad,
}

// ── BiosVtable trait ─────────────────────────────────────────────────────────

/// BiosVtable — abstract interface to BIOS services used by 86-DOS.
///
/// Every far call in 86DOS.asm of the form `CALL BIOSxxx,BIOSSEG` maps
/// to one method here.  The BIOS segment (BIOSSEG = 0x40) is implicit.
///
/// ASM BIOS calls:
///   BIOSSTAT    — console status             86DOS.asm:~2968
///   BIOSIN      — console input              86DOS.asm:~2975
///   BIOSOUT     — console output             86DOS.asm:~2853
///   BIOSPRINT   — list/printer output        86DOS.asm:~3001
///   BIOSAUXIN   — auxiliary input            86DOS.asm:~359
///   BIOSAUXOUT  — auxiliary output           86DOS.asm:~363
///   BIOSREAD    — disk sector read           86DOS.asm:~1095
///   BIOSWRITE   — disk sector write          86DOS.asm:~1150
///   BIOSDSKCHG  — disk-change detection      86DOS.asm:~766
pub trait BiosVtable {
    /// BIOSSTAT: returns 0 if no character ready, nonzero if one is waiting.
    /// ASM: CALL BIOSSTAT,BIOSSEG  86DOS.asm:~2968
    fn stat(&mut self) -> u8;

    /// BIOSIN: read one character from the console (blocks until available).
    /// ASM: CALL BIOSIN,BIOSSEG  86DOS.asm:~2975
    fn input(&mut self) -> u8;

    /// BIOSOUT: write one character to the console output device.
    /// ASM: CALL BIOSOUT,BIOSSEG  86DOS.asm:~2853
    fn output(&mut self, c: u8);

    /// BIOSPRINT: write one character to the list (printer) device.
    /// ASM: CALL BIOSPRINT,BIOSSEG  86DOS.asm:~3001
    fn print(&mut self, c: u8);

    /// BIOSAUXIN: read one character from the auxiliary (serial) port.
    /// ASM: CALL BIOSAUXIN,BIOSSEG  86DOS.asm:~359
    fn aux_in(&mut self) -> u8;

    /// BIOSAUXOUT: write one character to the auxiliary (serial) port.
    /// ASM: CALL BIOSAUXOUT,BIOSSEG  86DOS.asm:~363
    fn aux_out(&mut self, c: u8);

    /// BIOSREAD: read `count` sectors from `drive` starting at `sector` into buf.
    /// Returns true (carry set) on error, false on success.
    /// ASM: CALL BIOSREAD,BIOSSEG  86DOS.asm:~1095
    fn disk_read(&mut self, drive: u8, buf: &mut [u8], sector: u16, count: u16) -> bool;

    /// BIOSWRITE: write `count` sectors to `drive` starting at `sector` from buf.
    /// Returns true (carry set) on error, false on success.
    /// ASM: CALL BIOSWRITE,BIOSSEG  86DOS.asm:~1150
    fn disk_write(&mut self, drive: u8, buf: &[u8], sector: u16, count: u16) -> bool;

    /// BIOSDSKCHG: query whether the disk in `drive` has changed.
    /// Returns +1 = no change, 0 = unknown, -1 = changed.
    /// ASM: CALL BIOSDSKCHG,BIOSSEG  86DOS.asm:~766
    fn disk_change(&mut self, drive: u8) -> i8;
}

// ── Dpb (Drive Parameter Block) ──────────────────────────────────────────────

/// Dpb — Drive Parameter Block.
///
/// ASM: one DPB allocated per drive starting at MEMSTRT  86DOS.asm:3306-3423
///
/// Fields correspond 1:1 to the DPB fields stored by PERDRV:
///   DRVNUM    — physical drive number passed to BIOS
///   SECSIZ    — bytes per sector (LODW from DPT, 86DOS.asm:3311)
///   CLUSMSK   — sectors-per-cluster − 1  (LODB − 1, 86DOS.asm:3320)
///   CLUSSHFT  — log₂(sectors-per-cluster)  (computed, 86DOS.asm:3321-3333)
///   FIRFAT    — sector number of first FAT copy (86DOS.asm:3336)
///   FATCNT    — number of FAT copies (86DOS.asm:3338)
///   MAXENT    — maximum directory entries (86DOS.asm:3340)
///   FIRREC    — first data record sector (86DOS.asm:3343)
///   MAXCLUS   — highest valid cluster number (set by FIGMAX)
///   FATSIZ    — sectors per FAT copy (set by FIGFATSIZ)
///   FIRDIR    — first directory sector (86DOS.asm:3344)
///   dirtyfat  — 0=clean, 1=dirty, 0xFF=never read
///   dirsiz    — 0xFF=small 16-byte entries, 0=large 32-byte entries
///   fat       — in-memory FAT image (allocated by PERDRV)
#[derive(Clone, Debug)]
pub struct Dpb {
    /// DRVNUM — physical drive number (0=A, 1=B, …)
    pub drvnum: u8,
    /// SECSIZ — bytes per logical sector
    pub secsiz: u16,
    /// CLUSMSK — (sectors per cluster) − 1; used for cluster-boundary masking
    pub clusmsk: u8,
    /// CLUSSHFT — log₂(sectors per cluster); used for SHR in sector arithmetic
    pub clusshft: u8,
    /// FIRFAT — logical sector number of the first FAT
    pub firfat: u16,
    /// FATCNT — number of FAT copies on disk
    pub fatcnt: u8,
    /// MAXENT — maximum number of root directory entries
    pub maxent: u16,
    /// FIRREC — logical sector of first data cluster (cluster 2)
    pub firrec: u16,
    /// MAXCLUS — highest valid cluster number (computed by FIGMAX)
    pub maxclus: u16,
    /// FATSIZ — number of sectors per FAT copy (computed by FIGFATSIZ)
    pub fatsiz: u8,
    /// FIRDIR — logical sector of the root directory
    pub firdir: u16,
    // SMALLDIR fields (always compiled in; SMALLDIR=1 per project rules)
    /// FIRREC1 — SMALLDIR alternate firrec (drive B side 1)
    pub firrec1: u16,
    /// MAXCLUS1 — SMALLDIR alternate maxclus (drive B side 1)
    pub maxclus1: u16,
    /// FIRREC2 — SMALLDIR alternate firrec (drive B side 2)
    pub firrec2: u16,
    /// MAXCLUS2 — SMALLDIR alternate maxclus (drive B side 2)
    pub maxclus2: u16,
    /// DIRTYFAT — 0=clean, 1=needs flush, 0xFF=not yet read from disk
    pub dirtyfat: u8,
    /// DIRSIZ — 0xFF=small (16-byte) directory entries, 0=large (32-byte)
    pub dirsiz: u8,
    /// fat — in-memory FAT image; fatsiz × secsiz bytes
    pub fat: Vec<u8>,
}

impl Default for Dpb {
    fn default() -> Self {
        Dpb {
            drvnum: 0,
            secsiz: 512,
            clusmsk: 0,
            clusshft: 0,
            firfat: 1,
            fatcnt: 2,
            maxent: 64,
            firrec: 8,
            maxclus: 354,
            fatsiz: 2,
            firdir: 3,
            firrec1: 0,
            maxclus1: 0,
            firrec2: 0,
            maxclus2: 0,
            dirtyfat: 0xFF,
            dirsiz: 0,
            fat: Vec::new(),
        }
    }
}

// ── Fcb (File Control Block) ──────────────────────────────────────────────────

/// Fcb — File Control Block.
///
/// ASM: FCB DS 2 (pointer to user FCB)  86DOS.asm:3261
///      FCB layout described in 86DOS.asm:~707-756 (OPEN/DOOPEN)
///
/// The FCB is the primary file descriptor for all DOS 1.x file functions.
/// Fields:
///   drive     — 1-based drive (0=default); set by MAKEFCB / OPEN
///   name      — 11-byte filename+extension (8+3, space-padded)
///   extent    — current extent number (incremented by CLOSE)
///   recsiz    — logical record size in bytes (default 128)
///   filsiz    — file size in bytes (set by OPEN/CREATE)
///   fdate     — file date (packed BCD; set at CREATE/CLOSE)
///   fildirblk — directory block number of this file's entry
///   firclus   — first cluster of the file
///   lstclus   — last cluster reached during sequential I/O
///   cluspos   — cluster-chain offset corresponding to lstclus
///   dirtyfil  — 1 if file data has been written (triggers FATWRT on CLOSE)
///   nr        — next record number for sequential access
///   rr        — 3-byte random record number
#[repr(C)]
#[derive(Clone, Debug, Default)]
pub struct Fcb {
    /// drive — 0=default drive, 1=A, 2=B, …
    pub drive: u8,
    /// name — 8-byte filename + 3-byte extension, space-padded
    pub name: [u8; 11],
    /// extent — current file extent (each extent = 128 × recsiz bytes)
    pub extent: u16,
    /// recsiz — logical record size in bytes; default 128
    pub recsiz: u16,
    /// filsiz — file size in bytes (32-bit)
    pub filsiz: u32,
    /// fdate — packed file date (set by DOS at CREATE / CLOSE)
    pub fdate: u16,
    /// fildirblk — directory block number of this file's directory entry
    pub fildirblk: u16,
    /// firclus — first cluster of the file chain (0 = empty file)
    pub firclus: u16,
    /// lstclus — last cluster walked to during sequential I/O
    pub lstclus: u16,
    /// cluspos — position in cluster chain of lstclus
    pub cluspos: u16,
    /// dirtyfil — set to 1 when file data has been written
    pub dirtyfil: u8,
    /// nr — next sequential record number for SEQRD / SEQWRT
    pub nr: u8,
    /// rr — 3-byte random record number for RNDRD / RNDWRT
    pub rr: [u8; 3],
}

// ── Directory entry layouts ───────────────────────────────────────────────────

/// SmallDirEntry — 16-byte directory entry (SMALLDIR=1 compilation).
///
/// ASM: SMALLDIR EQU 1 conditional assembly  86DOS.asm:~3306
///
/// Fields:
///   name    — 8-byte base name (space-padded)
///   ext     — 3-byte extension (space-padded)
///   attr    — file attribute byte (bit 0 = read-only, bit 1 = system, …)
///   firclus — first FAT cluster of the file
///   size    — file size in bytes (16-bit; max 64 KB per file)
#[repr(C)]
#[derive(Clone, Debug, Default)]
pub struct SmallDirEntry {
    /// name — 8-byte base filename, space-padded
    pub name: [u8; 8],
    /// ext — 3-byte file extension, space-padded
    pub ext: [u8; 3],
    /// attr — file attribute byte
    pub attr: u8,
    /// firclus — first FAT12 cluster of file data
    pub firclus: u16,
    /// size — file size in bytes (16-bit for SMALLDIR)
    pub size: u16,
}

/// LargeDirEntry — standard 32-byte directory entry (SMALLDIR=0).
///
/// ASM: large (non-SMALLDIR) layout  86DOS.asm:~3306 (else branch)
///
/// Fields are the same as SmallDirEntry plus reserved, time, date, and
/// a 32-bit file size.
#[repr(C)]
#[derive(Clone, Debug, Default)]
pub struct LargeDirEntry {
    /// name — 8-byte base filename, space-padded
    pub name: [u8; 8],
    /// ext — 3-byte file extension, space-padded
    pub ext: [u8; 3],
    /// attr — file attribute byte
    pub attr: u8,
    /// reserved — 10 bytes reserved (zero in DOS 1.x)
    pub reserved: [u8; 10],
    /// time — packed file time (hour:minute:second/2)
    pub time: u16,
    /// date — packed file date (year-1980:month:day)
    pub date: u16,
    /// firclus — first FAT12 cluster of file data
    pub firclus: u16,
    /// size — file size in bytes (32-bit)
    pub size: u32,
}
