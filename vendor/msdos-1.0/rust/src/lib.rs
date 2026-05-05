//! lib.rs — 86-DOS 1.00 Rust translation: crate root, module declarations, DosState.
//!
//! Translated from 86DOS.asm.  This file corresponds to the global data area:
//!
//!   DATA AREA   86DOS.asm:3206-3295  — all kernel globals (CARPOS … SECCNT)
//!
//! DosState holds every global variable from the ASM data area that is
//! referenced by more than one subsystem.  Each field carries the ASM
//! label name as its doc-comment, along with the line in 86DOS.asm where
//! it is defined.

pub mod console;
pub mod directory;
pub mod disk;
pub mod fat;
pub mod fcb_util;
pub mod file;
pub mod init;
pub mod io;
pub mod syscall;
pub mod types;

use types::{BiosVtable, Dpb};

/// DosState — all kernel globals from the 86DOS.asm data area.
///
/// ASM: DATA AREA  86DOS.asm:3206-3295
///
/// Every field maps directly to a labelled storage location in the ASM.
/// Field names use Rust snake_case; the original ASM label is noted in
/// the field doc-comment.  Segment-relative fields (e.g. DMAADD, BUFFER)
/// are represented as flat Vec<u8> or integer offsets.
pub struct DosState {
    /// BIOS vtable — abstract interface replacing the far-call table at BIOSSEG.
    /// ASM: CALL BIOSxxx,BIOSSEG  86DOS.asm:~2853-3004
    pub bios: Box<dyn BiosVtable>,

    /// DRVTAB — table of pointers to per-drive DPBs.
    /// ASM: DRVTAB DS 30  86DOS.asm:3237  (enough for 15 drives)
    pub drives: Vec<Dpb>,

    /// CURDRVPT — pointer to current drive variable; here stored as index.
    /// ASM: CURDRVPT DS 2  86DOS.asm:3236
    pub cur_drive: usize,

    /// DMAADD — disk transfer address (offset + segment in ASM).
    /// ASM: DMAADD DW 80H  86DOS.asm:3228
    pub dma_addr: Vec<u8>,

    /// DMAADD+2 — DMA segment word (high part of DMAADD double-word).
    /// ASM: DS 2 after DMAADD  86DOS.asm:3229
    pub dma_seg: u16,

    /// DMAADD — DMA offset word (low part of DMAADD).
    pub dma_off: u16,

    /// BUFSECNO — logical sector number currently held in the sector buffer.
    /// ASM: BUFSECNO DW 0  86DOS.asm:3232
    pub buf_sec_no: u16,

    /// BUFDRVNO — drive number of the buffered sector; 0xFF = none cached.
    /// ASM: BUFDRVNO DB -1  86DOS.asm:3233
    pub buf_drv_no: u8,

    /// DIRTYBUF — set when the sector buffer contains unwritten data.
    /// ASM: DIRTYBUF DB 0  86DOS.asm:3234
    pub dirty_buf: bool,

    /// DIRBUF — directory sector buffer (overlaps INITCODE in ASM).
    /// ASM: DIRBUF (label at end of data area)  86DOS.asm:3295
    pub dir_buf: Vec<u8>,

    /// DIRBUFID — sector number currently in dir_buf; 0xFFFF = none.
    /// ASM: DIRBUFID DW -1  86DOS.asm:3235
    pub dir_buf_id: u16,

    /// BUFFER — pointer to main sector buffer (set by CONTINIT).
    /// ASM: BUFFER DS 2  86DOS.asm:3231
    pub buffer: Vec<u8>,

    /// DATE — packed system date (set by GETDAT / DOSINIT).
    /// ASM: DATE DS 2  86DOS.asm:3236
    pub date: u16,

    /// ENDMEM — top of available memory in paragraphs (set by SETMEM).
    /// ASM: ENDMEM DS 2  86DOS.asm:3230
    pub end_mem: u16,

    /// MAXSEC — maximum sector size across all installed drives.
    /// ASM: MAXSEC DW 0  86DOS.asm:3230
    pub max_sec: u16,

    /// CARPOS — current cursor column position (for tab/backspace handling).
    /// ASM: CARPOS DB 0  86DOS.asm:3219
    pub car_pos: u8,

    /// STARTPOS — cursor column at the start of the current input line.
    /// ASM: STARTPOS DB 0  86DOS.asm:3220
    pub start_pos: u8,

    /// PFLAG — printer-echo flag; nonzero means console output also goes to LIST.
    /// ASM: PFLAG DB 0  86DOS.asm:3221
    pub pflag: u8,

    /// DIRTYDIR — directory buffer dirty flag; nonzero means dir_buf needs flush.
    /// ASM: DIRTYDIR DB 0  86DOS.asm:3222
    pub dirty_dir: u8,

    /// NUMDRV — number of installed drives (set by DOSINIT).
    /// ASM: NUMDRV DS 1  86DOS.asm:3223
    pub num_drv: u8,

    /// CONTPOS — template continuation position for console line editing.
    /// ASM: CONTPOS DW 0  86DOS.asm:3224
    pub cont_pos: u16,

    /// FUNC — currently executing DOS function number (AH at dispatch time).
    /// ASM: FUNC DS 1  86DOS.asm:3250
    pub func: u8,

    /// LASTENT — last directory entry index used by SRCHFRST / SRCHNXT.
    /// ASM: LASTENT DS 2  86DOS.asm:3251
    pub last_ent: u16,

    /// NAME1 — first 11-byte name workspace (used by MAKEFCB / LODNAME).
    /// ASM: NAME1 DS 11  86DOS.asm:3255
    pub name1: [u8; 11],

    /// NAME2 — second 11-byte name workspace (rename target etc.).
    /// ASM: NAME2 DS 11  86DOS.asm:3256
    pub name2: [u8; 11],

    /// SPSAVE — saved caller SP (not meaningful in Rust).
    /// ASM: SPSAVE DS 2  86DOS.asm:3258
    pub sp_save: u16,

    /// SSSAVE — saved caller SS (not meaningful in Rust).
    /// ASM: SSSAVE DS 2  86DOS.asm:3259
    pub ss_save: u16,

    /// SECCLUSPOS — sector offset within the current cluster during I/O.
    /// ASM: SECCLUSPOS DS 1  86DOS.asm:3260
    pub seccluspos: u8,

    /// DSKERR — disk error code from the last BIOS disk call.
    /// ASM: DSKERR DS 1  86DOS.asm:3261
    pub dskerr: u8,

    /// TRANS — transfer-in-progress flag; nonzero while a DMA transfer is active.
    /// ASM: TRANS DS 1  86DOS.asm:3262
    pub trans: u8,

    /// FCB — index/pointer to the current user File Control Block.
    /// ASM: FCB DS 2  86DOS.asm:3263  (segment:offset pointer in real DOS)
    pub fcb_ptr: usize,

    /// NEXTADD — next free memory address (paragraph) for allocation.
    /// ASM: NEXTADD DS 2  86DOS.asm:3264
    pub next_add: u16,

    /// RECPOS — 4-byte byte position in file of current record access.
    /// ASM: RECPOS DS 4  86DOS.asm:3265
    pub rec_pos: u32,

    /// RECCNT — record count for block read/write operations.
    /// ASM: RECCNT DS 2  86DOS.asm:3266
    pub rec_cnt: u16,

    /// LASTPOS — last file position reached (used by OPTIMIZE / STORE).
    /// ASM: LASTPOS DS 2  86DOS.asm:3267
    pub last_pos: u16,

    /// CLUSNUM — current cluster number during sequential I/O.
    /// ASM: CLUSNUM DS 2  86DOS.asm:3268
    pub clus_num: u16,

    /// SECPOS — logical sector of the first sector accessed in the current operation.
    /// ASM: SECPOS DS 2  86DOS.asm:3269
    pub sec_pos: u16,

    /// VALSEC — number of previously-written (valid) sectors in a write sequence.
    /// ASM: VALSEC DS 2  86DOS.asm:3270
    pub val_sec: u16,

    /// BYTSECPOS — byte offset of the first byte within the current sector.
    /// ASM: BYTSECPOS DS 2  86DOS.asm:3271
    pub byt_sec_pos: u16,

    /// BYTPOS — byte position in the file of the current access (4 bytes).
    /// ASM: BYTPOS DS 4  86DOS.asm:3272
    pub byt_pos: u32,

    /// BYTCNT1 — byte count for the (possibly partial) first sector.
    /// ASM: BYTCNT1 DS 2  86DOS.asm:3273
    pub byt_cnt1: u16,

    /// BYTCNT2 — byte count for the (possibly partial) last sector.
    /// ASM: BYTCNT2 DS 2  86DOS.asm:3274
    pub byt_cnt2: u16,

    /// SECCNT — number of whole (full) sectors in the current transfer.
    /// ASM: SECCNT DS 2  86DOS.asm:3275
    pub sec_cnt: u16,

    /// INBUF / CONBUF — console input line template buffer.
    /// ASM: INBUF DS 15 / CONBUF DS 130  86DOS.asm:3238-3248
    pub in_buf: Vec<u8>,

    /// con_buf — secondary console output buffer.
    pub con_buf: Vec<u8>,
}

impl DosState {
    /// Create a new DosState with the given BIOS implementation and drive table.
    ///
    /// Mirrors the state left by DOSINIT/CONTINIT after boot:
    ///   buf_drv_no = 0xFF  (BUFDRVNO = -1, no sector cached)
    ///   end_mem    = 0xA000 (640 KB)
    ///   dma_addr   = 128-byte default DMA buffer at paragraph 0x80
    pub fn new(bios: Box<dyn BiosVtable>, drives: Vec<Dpb>) -> Self {
        DosState {
            bios,
            drives,
            cur_drive: 0,
            dma_addr: vec![0u8; 128],
            dma_seg: 0,
            dma_off: 0,
            buf_sec_no: 0,
            buf_drv_no: 0xFF,
            dirty_buf: false,
            dir_buf: vec![0u8; 512],
            dir_buf_id: 0,
            buffer: vec![0u8; 512],
            date: 0,
            end_mem: 0xA000,
            max_sec: 512,
            car_pos: 0,
            start_pos: 0,
            pflag: 0,
            dirty_dir: 0,
            num_drv: 0,
            cont_pos: 0,
            func: 0,
            last_ent: 0,
            name1: [b' '; 11],
            name2: [b' '; 11],
            sp_save: 0,
            ss_save: 0,
            seccluspos: 0,
            dskerr: 0,
            trans: 0,
            fcb_ptr: 0,
            next_add: 0,
            rec_pos: 0,
            rec_cnt: 0,
            last_pos: 0,
            clus_num: 0,
            sec_pos: 0,
            val_sec: 0,
            byt_sec_pos: 0,
            byt_pos: 0,
            byt_cnt1: 0,
            byt_cnt2: 0,
            sec_cnt: 0,
            in_buf: Vec::new(),
            con_buf: Vec::new(),
        }
    }
}
