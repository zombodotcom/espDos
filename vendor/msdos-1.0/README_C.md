# 86-DOS → C Translation Project

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

Produce a **functionally equivalent C implementation** that:

1. Can be read side-by-side with `86DOS.asm` — every C function traces back to a named
   ASM label and a line range.
2. Mirrors the original logic as faithfully as possible; readability and modern style are
   secondary to fidelity.
3. Compiles cleanly with a standard C99 (or later) compiler.
4. Makes the kernel's algorithms and data structures accessible to readers who do not
   read 8086 assembly.

The C version is **not** intended to run on real hardware or to boot an actual machine;
it is a structured annotation of the original code.  Where the ASM relies on hardware
behaviour that cannot be expressed in portable C (interrupt vectors, segment registers,
in-circuit BIOS calls) the code uses stub functions, function pointers, or clearly
marked `/* HARDWARE */` comments.

---

## Repository layout

```
msdos-1.0/
├── 86DOS.asm                 # original source — read-only, never modified
├── README_C.md               # this file
├── AGENTS.md                 # rules for AI agents
├── Makefile                  # builds all C sources and tests
├── include/
│   ├── dos_types.h           # stdint aliases, segment-model helpers, common macros
│   ├── fcb.h                 # File Control Block field offsets  (ASM lines 60-73)
│   ├── dpb.h                 # Drive Parameter Block             (ASM lines 103-127)
│   ├── bios.h                # BIOS entry-point layout & vtable  (ASM lines 132-143)
│   └── dos.h                 # public syscall API (function numbers, prototypes)
├── src/
│   ├── main.c                # DOSINIT — top-level initialisation (ASM ~3270-3560)
│   ├── init.c                # drive-table build, interrupt-vector setup, memory scan
│   ├── syscall.c             # COMMAND/ENTRY dispatcher + DISPATCH table (ASM 199-346)
│   ├── fat.c                 # UNPACK, PACK, FIGFAT, FATWRT, FATREAD, RELEASE,
│   │                         #   ALLOCATE, FNDCLUS  (ASM 369-432, 914-960, ~2700-2900)
│   ├── directory.c           # GETFILE, GETENTRY, NEXTENTRY, DIRREAD, DIRWRITE,
│   │                         #   STARTSRCH, CHKDIRWRITE  (ASM 448-599, 1071-1093)
│   ├── file.c                # OPEN, CLOSE, CREATE, DELETE, RENAME  (ASM 602-1068)
│   ├── disk.c                # DREAD, DWRITE, sector buffer  (ASM 1095-~1200)
│   ├── io.c                  # SEQRD, SEQWRT, RNDRD, RNDWRT, BLKRD, BLKWRT,
│   │                         #   SETDMA, OPTIMIZE  (ASM ~1200-2100)
│   ├── console.c             # CONIN, CONOUT, BUFIN, RAWIO, CONSTAT, CRLF, OUTMES,
│   │                         #   template line-editing  (ASM ~2100-2700)
│   └── fcb_util.c            # MAKEFCB, SETRNDREC, FILESIZE, MOVNAME  (ASM ~2200-2400)
└── tests/
    ├── test_fat.c            # unit tests for UNPACK / PACK
    ├── test_directory.c      # unit tests for directory search
    └── test_io.c             # unit tests for sequential/random record I/O
```

---

## Data structures

### File Control Block (FCB)  —  `include/fcb.h`  —  ASM lines 60-73

The FCB is the primary file handle.  Its layout is fixed at 36 bytes:

| Offset | Size | Field       | Description                              |
|--------|------|-------------|------------------------------------------|
|  0     |  1   | drive       | Drive code (0 = current)                 |
|  1     | 11   | name        | 8.3 filename, space-padded               |
| 12     |  2   | EXTENT      | Extent number                            |
| 14     |  2   | RECSIZ      | Logical record size (user-settable)      |
| 16     |  4   | FILSIZ      | File size in bytes                       |
| 20     |  2   | FDATE       | Date of last write                       |
| 22     |  2   | FILDIRBLK   | Directory entry number                   |
| 24     |  2   | FIRCLUS     | First cluster of file                    |
| 26     |  2   | LSTCLUS     | Last cluster accessed                    |
| 28     |  2   | CLUSPOS     | Position of last cluster accessed        |
| 30     |  1   | DIRTYFIL    | Non-zero if file has been written        |
| 32     |  1   | NR          | Next sequential record number            |
| 33     |  3   | RR          | Random record number (24-bit)            |

### Drive Parameter Block (DPB)  —  `include/dpb.h`  —  ASM lines 103-127

One DPB per logical drive; allocated in the DOS data segment at initialisation:

| Offset | Field      | Description                                   |
|--------|------------|-----------------------------------------------|
|  0     | DRVNUM     | Drive number                                  |
|  1     | SECSIZ     | Physical sector size in bytes                 |
|  3     | CLUSMSK    | Sectors/cluster − 1                           |
|  4     | CLUSSHFT   | log₂(sectors/cluster)                        |
|  5     | FIRFAT     | First sector of FAT area                      |
|  7     | FATCNT     | Number of FAT copies                          |
|  8     | MAXENT     | Maximum directory entries                     |
| 10     | FIRREC     | First data sector                             |
| 12     | MAXCLUS    | Total clusters + 1                            |
| 14     | FATSIZ     | Sectors occupied by one FAT copy              |
| 15     | FIRDIR     | First directory sector                        |
| 17+    | (SMALLDIR) | Extra fields for 16-byte directory support    |
| last   | DIRTYFAT   | FAT dirty flag (0=clean, 1=dirty, FF=unread)  |
| FAT[]  | FAT        | In-memory FAT image immediately follows       |

### 32-byte directory entry  —  ASM lines 76-98

| Offset | Size | Description                                        |
|--------|------|----------------------------------------------------|
|  0     | 11   | Name + extension (0E5h = deleted)                  |
| 11     | 13   | Reserved (zero)                                    |
| 24     |  2   | Date (bits 0-4=day, 5-8=month, 9-15=year−1980)    |
| 26     |  2   | First cluster                                      |
| 28     |  4   | File size in bytes                                 |

---

## Syscall dispatch table  —  `src/syscall.c`  —  ASM lines 302-346

The original uses a 41-entry word table (`DISPATCH`) indexed by function number.
In C this becomes an array of function pointers.

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

## FAT12 encoding  —  `src/fat.c`  —  ASM lines 369-432

Each cluster entry is 12 bits, packed two entries per three bytes.  For cluster N:

```
byte_offset = N + (N >> 1)       /* = floor(N * 1.5) */
word        = FAT[byte_offset]   /* little-endian 16-bit fetch */
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

`UNPACK` (ASM 369-395) reads an entry; `PACK` (ASM 402-432) writes one.

---

## Key translation challenges

### 1. Segment registers

86-DOS uses three distinct 64 KB segments simultaneously:

- `CS` — code + all kernel data (the DOS segment)
- `DS` — points to user memory (FCB, DMA buffer) during system calls
- `ES` — used as a second scratch segment; often set to CS during internal operations

In the C translation, these are collapsed into a single flat address space.  Pointers
that in the ASM are `SEG CS` references become plain C pointers into a global `dos_state`
structure.  A comment `/* SEG CS */` marks every such access.

### 2. Flag-based return conventions

Many ASM routines communicate results through CPU flags:

- **Carry set** → error / not found
- **Zero set** → result is zero
- **Sign set** → negative / special condition

In C these become explicit `int` return values: `0` = success / found, `-1` = error /
not found, matching the DOS convention visible at the syscall boundary.

### 3. The ENTRY / CALL-5 mechanism  —  ASM lines 206-300

CP/M programs called DOS via `CALL 5` (a far call to segment 0, offset 5).  86-DOS
re-orders the stack at `ENTRY` to look as if an `INT` was used, then falls through to
`SAVREGS`.  In C this is modelled as a regular function call through `dos_dispatch()`.

### 4. Self-modifying code — CSLOC  —  ASM line 250

The instruction at label `CSLOC` is overwritten at runtime with the caller's CS value so
that `IRET` returns to the correct segment.  In the C model this is replaced with a
function-pointer variable `cs_return_seg` that is set on each syscall entry.

### 5. In-memory FAT allocation

The FAT for each drive is stored directly after the DPB in memory (the `FAT:` label at
ASM line 126 is immediately after `DIRTYFAT`).  In C, `dpb_t` ends with a flexible array
member `uint8_t fat[]` to preserve this layout.

### 6. BIOS interface  —  ASM lines 132-143

All hardware I/O goes through jump-stubs in segment `40H` (BIOSSEG).  In C these are
collected into a `bios_vtable_t` struct of function pointers, allowing the core logic to
be tested with a software BIOS mock.

### 7. 16-byte vs 32-byte directory entries (SMALLDIR flag)

When `SMALLDIR=1` (as in this file) the kernel accepts both 16-byte (old) and 32-byte
(new) directory entries.  The `DIRSIZ` field in the DPB records which format is active
(`-1` = small, `0` = large).  All conditional `IF SMALLDIR` blocks in the ASM are
translated to `if (dpb->dirsiz == -1)` branches in C.

---

## Build instructions

```
cd msdos-1.0
make          # builds build/libdos.a from all 9 source files
make check    # builds and runs all unit tests (11 tests, 0 failures)
make clean    # removes build/
```

Requires: any C99-compatible compiler (`gcc`, `clang`, `tcc`).
No external libraries beyond the C standard library.

---

## Implementation phases

| Phase | Files | Status |
|-------|-------|--------|
| 1 — Headers | `include/dos_types.h`, `fcb.h`, `dpb.h`, `bios.h`, `dos.h` | **complete** |
| 2 — FAT12 | `src/fat.c` | **complete** |
| 3 — Disk/buffer | `src/disk.c` | **complete** |
| 4 — Directory | `src/directory.c` | **complete** |
| 5 — File ops | `src/file.c` | **complete** |
| 6 — Record I/O | `src/io.c` | **complete** |
| 7 — Console | `src/console.c` | **complete** |
| 8 — FCB utilities | `src/fcb_util.c` | **complete** |
| 9 — Dispatcher | `src/syscall.c` | **complete** |
| 10 — Init | `src/init.c` | **complete** |
| 11 — Build system | `Makefile` | **complete** |
| 12 — Tests | `tests/*.c` (11 tests, 0 failures) | **complete** |
| 13 — Annotation pass | all files | planned |

---

## Reference

- Original source: `86DOS.asm` in this directory
- Tim Paterson's account of writing DOS:
  <https://web.archive.org/web/20210306031205/http://dosmandrivel.blogspot.com/>
- "The Origins of DOS" — Paul Allen memo, 1981
- _The MS-DOS Encyclopedia_, Microsoft Press, 1988
