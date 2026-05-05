# 86-DOS → Rust Translation Project

## Background

`86DOS.asm` is the complete source of **86-DOS version 1.00**, written by Tim Paterson
at Seattle Computer Products and dated April 28, 1981.  It is a single 3 622-line 8086
assembly file that implements an entire operating system kernel — the direct ancestor of
MS-DOS 1.0 and, through it, every version of DOS and early Windows.

The revision history embedded in the file records the evolution from version 0.34
(December 1980) through 1.00 (April 1981):

```
0.34  12/29/80  General release
0.42  02/25/81  32-byte directory entries
0.56  03/23/81  Variable record and sector sizes
0.60  03/27/81  Ctrl-C exit, register save on user stack
0.74  04/15/81  Named I/O devices (CON, PRN, AUX, LST)
0.75  04/17/81  Buffer handling improvements
0.76  04/23/81  Directory size fix for non-power-of-2 entries
0.80  04/27/81  Console input without echo (Functions 7 & 8)
1.00  04/28/81  Renumbered for general release
```

---

## Goal

Produce a **functionally equivalent Rust implementation** that:

1. Can be read side-by-side with `86DOS.asm` — every Rust function traces back to a named
   ASM label and a line range.
2. Mirrors the original logic as faithfully as possible; idiomatic Rust style is secondary
   to fidelity.
3. Compiles cleanly with `cargo build` (stable Rust, no unsafe where avoidable).
4. Makes the kernel's algorithms and data structures accessible to readers who do not
   read 8086 assembly.

The Rust version is **not** intended to run on real hardware or to boot an actual machine;
it is a structured annotation of the original code.  Where the ASM relies on hardware
behaviour that cannot be expressed in safe Rust (interrupt vectors, segment registers,
in-circuit BIOS calls) the code uses trait objects, function pointers, or clearly marked
`// HARDWARE` comments.

---

## Repository layout

```
msdos-1.0/
├── 86DOS.asm                 # original source — read-only, never modified
├── README_Rust.md            # this file
├── AGENTS.md                 # rules for AI agents
├── rust/
│   ├── Cargo.toml
│   ├── src/
│   │   ├── lib.rs            # crate root; module declarations; DosState
│   │   ├── types.rs          # u8/u16/u32 aliases, FCB, DPB, BiosVtable structs
│   │   ├── fat.rs            # UNPACK, PACK, FIGFAT, FATWRT, FATREAD, RELEASE,
│   │   │                     #   ALLOCATE, FNDCLUS  (ASM 369-432, 914-960, 2169-2301)
│   │   ├── disk.rs           # DREAD, DWRITE, BUFSEC, BUFRD, BUFWRT, NEXTSEC, HARDERR
│   │   │                     #   (ASM 1095-1706)
│   │   ├── directory.rs      # GETFILE, GETENTRY, NEXTENTRY, DIRREAD, DIRWRITE,
│   │   │                     #   STARTSRCH, CHKDIRWRITE  (ASM 448-599, 1071-1093)
│   │   ├── file.rs           # OPEN, CLOSE, CREATE, DELETE, RENAME, MOVNAME, LODNAME,
│   │   │                     #   GETBP  (ASM 602-1068)
│   │   ├── io.rs             # SETUP, BREAKDOWN, LOAD, STORE, SEQRD, SEQWRT, RNDRD,
│   │   │                     #   RNDWRT, BLKRD, BLKWRT, GETREC, WRTEOF, FIGREC
│   │   │                     #   (ASM 1256-2125, 2126-2168)
│   │   ├── console.rs        # CONIN, CONOUT, BUFIN, RAWIO, CONSTAT, CRLF, OUTMES,
│   │   │                     #   template line-editing  (ASM 2566-2651)
│   │   ├── fcb_util.rs       # MAKEFCB, SETRNDREC, FILESIZE, SRCHFRST, SRCHNXT,
│   │   │                     #   SAVPLCE, KILLSRCH, SRCHDEV  (ASM 2302-2564)
│   │   ├── syscall.rs        # COMMAND/ENTRY dispatcher + DISPATCH table (ASM 199-346)
│   │   └── init.rs           # DOSINIT, PERDRV, CONTINIT, FIGFATSIZ, FIGMAX
│   │                         #   (ASM 3296-3622)
│   └── tests/
│       ├── test_fat.rs       # unit tests for unpack / pack
│       ├── test_directory.rs # unit tests for directory search
│       └── test_io.rs        # unit tests for sequential/random record I/O
```

---

## Data structures

### File Control Block (FCB)  —  `src/types.rs`  —  ASM lines 60-73

The FCB is the primary file handle.  Its layout is fixed at 36 bytes:

| Offset | Size | Field       | Description                              |
|--------|------|-------------|------------------------------------------|
|  0     |  1   | drive       | Drive code (0 = current)                 |
|  1     | 11   | name        | 8.3 filename, space-padded               |
| 12     |  2   | extent      | Extent number                            |
| 14     |  2   | recsiz      | Logical record size (user-settable)      |
| 16     |  4   | filsiz      | File size in bytes                       |
| 20     |  2   | fdate       | Date of last write                       |
| 22     |  2   | fildirblk   | Directory entry number                   |
| 24     |  2   | firclus     | First cluster of file                    |
| 26     |  2   | lstclus     | Last cluster accessed                    |
| 28     |  2   | cluspos     | Position of last cluster accessed        |
| 30     |  1   | dirtyfil    | Non-zero if file has been written        |
| 32     |  1   | nr          | Next sequential record number            |
| 33     |  3   | rr          | Random record number (24-bit)            |

Represented in Rust as a `#[repr(C)]` struct with `u8`/`u16`/`u32` fields matching the
ASM field widths exactly.

### Drive Parameter Block (DPB)  —  `src/types.rs`  —  ASM lines 103-127

One DPB per logical drive:

| Offset | Field      | Description                                   |
|--------|------------|-----------------------------------------------|
|  0     | drvnum     | Drive number                                  |
|  1     | secsiz     | Physical sector size in bytes                 |
|  3     | clusmsk    | Sectors/cluster − 1                           |
|  4     | clusshft   | log₂(sectors/cluster)                        |
|  5     | firfat     | First sector of FAT area                      |
|  7     | fatcnt     | Number of FAT copies                          |
|  8     | maxent     | Maximum directory entries                     |
| 10     | firrec     | First data sector                             |
| 12     | maxclus    | Total clusters + 1                            |
| 14     | fatsiz     | Sectors occupied by one FAT copy              |
| 15     | firdir     | First directory sector                        |
| 17+    | (smalldir) | Extra fields for 16-byte directory support    |
| last   | dirtyfat   | FAT dirty flag (0=clean, 1=dirty, 0xFF=unread)|

The in-memory FAT image follows immediately after the DPB, mirroring the `FAT:` label at
ASM line 126.  In Rust this is modelled as a `Vec<u8>` field `fat` appended to `Dpb`.

### 32-byte directory entry  —  ASM lines 76-98

| Offset | Size | Description                                        |
|--------|------|----------------------------------------------------|
|  0     | 11   | Name + extension (0xE5 = deleted)                  |
| 11     | 13   | Reserved (zero)                                    |
| 24     |  2   | Date (bits 0-4=day, 5-8=month, 9-15=year−1980)    |
| 26     |  2   | First cluster                                      |
| 28     |  4   | File size in bytes                                 |

---

## Syscall dispatch table  —  `src/syscall.rs`  —  ASM lines 302-346

The original uses a 41-entry word table (`DISPATCH`) indexed by function number.
In Rust this becomes an array of function pointers (or a `match` arm per function).

| Number | Label      | Description                        |
|--------|------------|------------------------------------|
|  0     | ABORT      | Terminate program                  |
|  1     | CONIN      | Console input with echo            |
|  2     | CONOUT     | Console output                     |
|  3     | READER     | Aux/serial input                   |
|  4     | PUNCH      | Aux/serial output                  |
|  5     | LIST       | Printer output                     |
|  6     | RAWIO      | Raw console I/O                    |
|  7     | RAWINP     | Raw console input (no echo)        |
|  8     | IN         | Console input without echo         |
|  9     | PRTBUF     | Print string ($-terminated)        |
| 10     | BUFIN      | Buffered line input                |
| 11     | CONSTAT    | Console status                     |
| 12     | VERSION    | Get DOS version (stub → 0)         |
| 13     | DSKRESET   | Flush and reset disk system        |
| 14     | SELDSK     | Select default drive               |
| 15     | OPEN       | Open file via FCB                  |
| 16     | CLOSE      | Close file via FCB                 |
| 17     | SRCHFRST   | Search first directory match       |
| 18     | SRCHNXT    | Search next directory match        |
| 19     | DELETE     | Delete file                        |
| 20     | SEQRD      | Sequential read                    |
| 21     | SEQWRT     | Sequential write                   |
| 22     | CREATE     | Create file                        |
| 23     | RENAME     | Rename file                        |
| 24     | INUSE      | (reserved/stub)                    |
| 25     | CURDRV     | Get current drive                  |
| 26     | SETDMA     | Set DMA (transfer) address         |
| 27     | GETFATPT   | Get FAT pointer                    |
| 28     | WRTPROT    | Write-protect disk (stub)          |
| 29     | GETRDONLY  | Get read-only vector (stub)        |
| 30     | SETATTRIB  | Set file attribute (stub)          |
| 31     | GETDSKPT   | Get drive parameter pointer        |
| 32     | USERCODE   | Get/set user code (stub)           |
| 33     | RNDRD      | Random read                        |
| 34     | RNDWRT     | Random write                       |
| 35     | FILESIZE   | Compute file size in records       |
| 36     | SETRNDREC  | Set random record from sequential  |
| 37     | SETVECT    | Set interrupt vector               |
| 38     | NEWBASE    | Set new program base segment       |
| 39     | BLKRD      | Block read (multiple records)      |
| 40     | BLKWRT     | Block write (multiple records)     |
| 41     | MAKEFCB    | Parse filename into FCB            |

---

## FAT12 encoding  —  `src/fat.rs`  —  ASM lines 369-432

Each cluster entry is 12 bits, packed two entries per three bytes.  For cluster N:

```
byte_offset = N + (N >> 1)       // = floor(N * 1.5)
word        = FAT[byte_offset]   // little-endian 16-bit fetch
if N is even:  entry = word & 0x0FFF
if N is odd:   entry = word >> 4
```

Special entry values:

| Value       | Meaning                  |
|-------------|--------------------------|
| 0x000       | Free cluster             |
| 0x001       | Reserved                 |
| 0x002-0xFEF | Next cluster in chain    |
| 0xFF0-0xFF7 | Reserved                 |
| 0xFF8-0xFFF | End-of-file marker       |

`unpack` (ASM 369-395) reads an entry; `pack` (ASM 402-432) writes one.

---

## Key translation challenges

### 1. Segment registers

86-DOS uses three distinct 64 KB segments simultaneously:

- `CS` — code + all kernel data (the DOS segment)
- `DS` — points to user memory (FCB, DMA buffer) during system calls
- `ES` — used as a second scratch segment; often set to CS during internal operations

In the Rust translation, these are collapsed into a single flat address space.  A
`DosState` struct holds all kernel globals.  Pointers that in the ASM are `SEG CS`
references become plain Rust references into `DosState`.  A comment `// SEG CS` marks
every such access.

### 2. Flag-based return conventions

Many ASM routines communicate results through CPU flags:

- **Carry set** → error / not found
- **Zero set** → result is zero
- **Sign set** → negative / special condition

In Rust these are expressed as `Result<T, DosError>` or `Option<T>` return values,
matching the DOS convention visible at the syscall boundary.  `DosError::NotFound`
replaces carry-set, `DosError::InvalidDrive` replaces the carry from `GETBP`, etc.

### 3. The ENTRY / CALL-5 mechanism  —  ASM lines 206-300

CP/M programs called DOS via `CALL 5` (a far call to segment 0, offset 5).  86-DOS
re-orders the stack at `ENTRY` to look as if an `INT` was used, then falls through to
`SAVREGS`.  In Rust this is modelled as a regular function call through `dos_dispatch()`.

### 4. Self-modifying code — CSLOC  —  ASM line 250

The instruction at label `CSLOC` is overwritten at runtime with the caller's CS value so
that `IRET` returns to the correct segment.  In the Rust model this is replaced with a
field `cs_return_seg: u16` in `DosState` set on each syscall entry.

### 5. In-memory FAT allocation

The FAT for each drive is stored directly after the DPB in memory (the `FAT:` label at
ASM line 126 is immediately after `DIRTYFAT`).  In Rust, `Dpb` holds a `Vec<u8>` fat
field that is allocated at initialisation time with the correct byte count.

### 6. BIOS interface  —  ASM lines 132-143

All hardware I/O goes through jump-stubs in segment `40H` (BIOSSEG).  In Rust these are
collected into a `BiosVtable` struct of boxed closures / trait objects, allowing the core
logic to be tested with a software BIOS mock.

### 7. 16-byte vs 32-byte directory entries (SMALLDIR flag)

When `SMALLDIR=1` (as in this file) the kernel accepts both 16-byte (old) and 32-byte
(new) directory entries.  The `dirsiz` field in the DPB records which format is active
(`0xFF` / `u8::MAX` as `-1i8` = small, `0` = large).  All conditional `IF SMALLDIR`
blocks in the ASM are translated to `if dpb.dirsiz == 0xFF` branches in Rust.

### 8. Lifetimes and borrowing

The ASM keeps multiple live pointers into the same memory (e.g. `BX` into `DIRBUF` and
`SI` into the same entry).  In Rust this requires either splitting borrows explicitly or,
where the pattern is too intricate, using index integers instead of references.  No
`unsafe` is used; every such case carries a `// NOTE: differs from ASM` comment.

---

## Build instructions

```
cd msdos-1.0/rust
cargo build          # compiles the library crate
cargo test           # compiles and runs all unit tests
```

Requires: Rust stable (1.70 or later).  No external crates beyond `std`.

---

## Implementation phases

| Phase | Files | Status |
|-------|-------|--------|
| 1 — Types/structs | `src/types.rs`, `src/lib.rs` | **complete** |
| 2 — FAT12 | `src/fat.rs` | **complete** |
| 3 — Disk/buffer | `src/disk.rs` | **complete** |
| 4 — Directory | `src/directory.rs` | **complete** |
| 5 — File ops | `src/file.rs` | **complete** |
| 6 — Record I/O | `src/io.rs` | **complete** |
| 7 — Console | `src/console.rs` | **complete** |
| 8 — FCB utilities | `src/fcb_util.rs` | **complete** |
| 9 — Dispatcher | `src/syscall.rs` | **complete** |
| 10 — Init | `src/init.rs` | **complete** |
| 11 — Build system | `Cargo.toml` | **complete** |
| 12 — Tests | `tests/*.rs` | **complete** |

---

## Reference

- Original source: `86DOS.asm` in the parent directory
- Tim Paterson's account of writing DOS:
  <https://web.archive.org/web/20210306031205/http://dosmandrivel.blogspot.com/>
- "The Origins of DOS" — Paul Allen memo, 1981
- _The MS-DOS Encyclopedia_, Microsoft Press, 1988
