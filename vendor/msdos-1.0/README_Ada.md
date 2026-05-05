# 86-DOS → Ada Translation Project

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

Produce a **functionally equivalent Ada implementation** that:

1. Can be read side-by-side with `86DOS.asm` — every Ada subprogram traces back to a
   named ASM label and a line range.
2. Mirrors the original logic as faithfully as possible; idiomatic Ada style is secondary
   to fidelity.
3. Compiles cleanly with `gnatmake` (GNAT 14, Ada 2012, no external libraries).
4. Makes the kernel's algorithms and data structures accessible to readers who do not
   read 8086 assembly.

The Ada version is **not** intended to run on real hardware or to boot an actual machine;
it is a structured annotation of the original code.  Where the ASM relies on hardware
behaviour that cannot be expressed in Ada (interrupt vectors, segment registers, in-circuit
BIOS calls) the code uses abstract tagged types and clearly marked `-- HARDWARE` comments.

---

## Repository layout

```
msdos-1.0/
├── 86DOS.asm                   # original source — read-only, never modified
├── README_Ada.md               # this file
├── AGENTS.md                   # rules for AI agents
├── ada/
│   ├── Makefile
│   ├── src/
│   │   ├── dos86.ads / .adb    # root package: types, constants, DPB, FCB, Dos_State
│   │   ├── dos86-fat.ads / .adb       # UNPACK, PACK, FIGFAT, FATWRT, FATREAD,
│   │   │                              #   RELEASE, ALLOCATE, FNDCLUS  (ASM 369-432,
│   │   │                              #   914-960, 2169-2301)
│   │   ├── dos86-disk.ads / .adb      # DREAD, DWRITE, BUFSEC, BUFRD, BUFWRT,
│   │   │                              #   NEXTSEC, HARDERR  (ASM 1095-1706)
│   │   ├── dos86-directory.ads / .adb # GETFILE, GETENTRY, NEXTENTRY, STARTSRCH,
│   │   │                              #   MOVNAME, LODNAME, GETBP  (ASM 448-765)
│   │   ├── dos86-file_ops.ads / .adb  # OPEN, CLOSE, CREATE, DELETE, RENAME
│   │   │                              #   (ASM 602-1068)
│   │   ├── dos86-io_ops.ads / .adb    # LOAD, STORE, SEQRD, SEQWRT, RNDRD, RNDWRT,
│   │   │                              #   BLKRD, BLKWRT, GETREC, WRTEOF, FIGREC
│   │   │                              #   (ASM 1707-2168)
│   │   ├── dos86-console.ads / .adb   # CONIN, CONOUT, BUFIN, RAWIO, CONSTAT, CRLF,
│   │   │                              #   OUTMES  (ASM 2566-2651)
│   │   ├── dos86-fcb_util.ads / .adb  # MAKEFCB, SETRNDREC, FILESIZE, SRCHFRST,
│   │   │                              #   SRCHNXT  (ASM 2302-2564)
│   │   ├── dos86-syscall.ads / .adb   # COMMAND/ENTRY dispatcher + DISPATCH table
│   │   │                              #   (ASM 199-346)
│   │   └── dos86-init.ads / .adb      # DOSINIT, PERDRV, CONTINIT, FIGFATSIZ, FIGMAX
│   │                                  #   (ASM 3296-3622)
│   ├── tests/
│   │   ├── test_fat.adb        # unit tests for Unpack, Pack, Fnd_Clus, Fig_Rec
│   │   ├── test_directory.adb  # unit tests for Lod_Name, Get_Bp, Mov_Name
│   │   └── test_io.adb         # unit tests for Get_Rec, Fn_Set_Rnd_Rec, Fn_Set_DMA
│   └── build/                  # generated — gitignored
```

---

## Data structures

### Fixed-width integer types  —  `dos86.ads`

```ada
subtype Byte  is Interfaces.Unsigned_8;
subtype Word  is Interfaces.Unsigned_16;
subtype DWord is Interfaces.Unsigned_32;
```

These correspond to the 8-bit and 16-bit operands throughout the ASM; `DWord` appears only
in file-size and 32-bit record-number computations introduced at version 0.56.

### File Control Block (FCB)  —  `dos86.ads`  —  ASM lines 707-756

| Field      | Type           | Description                               |
|------------|----------------|-------------------------------------------|
| Drive      | Byte           | Drive code (0 = current)                  |
| Name       | Byte_Array(11) | 8.3 filename, space-padded                |
| Extent     | Word           | Extent number                             |
| Recsiz     | Word           | Logical record size (user-settable)       |
| Filsiz     | DWord          | File size in bytes                        |
| Fdate      | Word           | Date of last write                        |
| Fildirblk  | Word           | Directory entry number                    |
| Firclus    | Word           | First cluster of file                     |
| Lstclus    | Word           | Last cluster accessed                     |
| Cluspos    | Word           | Position of last cluster accessed         |
| Dirtyfil   | Byte           | Non-zero if file has been written         |
| Nr         | Byte           | Next sequential record number             |
| Rr         | Byte_Array(3)  | Random record number (24-bit LE)          |

### Drive Parameter Block (DPB)  —  `dos86.ads`  —  ASM lines 3306-3423

| Field      | Type       | Description                                     |
|------------|------------|-------------------------------------------------|
| Drvnum     | Byte       | Physical drive number                           |
| Secsiz     | Word       | Bytes per sector                                |
| Clusmsk    | Byte       | Sectors/cluster − 1                             |
| Clusshft   | Byte       | log₂(sectors/cluster)                          |
| Firfat     | Word       | First sector of FAT area                        |
| Fatcnt     | Byte       | Number of FAT copies                            |
| Maxent     | Word       | Maximum directory entries                       |
| Firrec     | Word       | First data sector                               |
| Maxclus    | Word       | Total clusters on disk                          |
| Fatsiz     | Byte       | Sectors per FAT copy                            |
| Firdir     | Word       | First directory sector                          |
| Dsksiz     | Word       | Total sectors on disk (ASM 3374)                |
| Dirtyfat   | Byte       | 0=clean, 1=dirty, 0xFF=never read               |
| Dirsiz     | Byte       | 0xFF=small 16-byte entries, 0=large 32-byte     |
| Fat        | Fat_Buffer | In-memory FAT image (up to 8×512 bytes)         |
| Fat_Size   | Natural    | Valid bytes in Fat                              |

### DOS global state  —  `dos86.ads`

All kernel variables from `86DOS.asm:3206-3268` are collected in a single `Dos_State`
record.  The package-level variable `Dos : Dos_State` provides the live kernel state,
matching the original's placement of all data in the CS segment.

---

## BIOS interface  —  `dos86.ads`  —  ASM lines 132-143

All hardware I/O in the ASM goes through far-calls to segment `40H` (BIOSSEG):
`BIOSREAD`, `BIOSWRITE`, `BIOSIN`, `BIOSOUT`, `BIOSDSKCHG`, etc.

In Ada these are abstract dispatching operations on a `Bios_Vtable` type:

```ada
type Bios_Vtable is abstract tagged limited null record;
function Disk_Read  (Bios : in out Bios_Vtable; ...) return Boolean is abstract;
function Disk_Write (Bios : in out Bios_Vtable; ...) return Boolean is abstract;
-- etc.
```

Test code provides a concrete instantiation with a software BIOS stub.

---

## Error handling

ASM routines communicate errors by setting the carry flag or jumping to `ERROR`.  In Ada
this is represented by the `Dos_Error` exception, raised with a descriptive message
string:

| Exception message | ASM condition               |
|-------------------|-----------------------------|
| `"NotFound"`      | File/entry not found        |
| `"InvalidDrive"`  | Drive >= NUMDRV             |
| `"DiskError"`     | BIOS I/O failure            |
| `"NoSpace"`       | No free cluster (ALLOCATE)  |
| `"BadFileName"`   | Illegal FCB name            |
| `"BadFat"`        | HURTFAT / FAT bounds error  |
| `"AllFatsBad"`    | All FAT copies unreadable   |
| `"BadCall"`       | Function number > MAXCALL   |

---

## Syscall dispatch table  —  `dos86-syscall.ads`  —  ASM lines 302-346

| Number | Constant        | Description                        |
|--------|-----------------|-------------------------------------|
|  0     | FN_ABORT        | Terminate program                   |
|  1     | FN_CONIN        | Console input with echo             |
|  2     | FN_CONOUT       | Console output                      |
|  3     | FN_READER       | Aux/serial input                    |
|  4     | FN_PUNCH        | Aux/serial output                   |
|  5     | FN_LIST         | Printer output                      |
|  6     | FN_RAWIO        | Raw console I/O                     |
|  7     | FN_RAWINP       | Raw console input (no echo)         |
|  8     | FN_IN           | Console input without echo          |
|  9     | FN_PRTBUF       | Print string ($-terminated)         |
| 10     | FN_BUFIN        | Buffered line input                 |
| 11     | FN_CONSTAT      | Console status                      |
| 12     | FN_VERSION      | Get DOS version (returns 0)         |
| 13     | FN_DSKRESET     | Flush and reset disk system         |
| 14     | FN_SELDSK       | Select default drive                |
| 15     | FN_OPEN         | Open file via FCB                   |
| 16     | FN_CLOSE        | Close file via FCB                  |
| 17     | FN_SRCHFRST     | Search first directory match        |
| 18     | FN_SRCHNXT      | Search next directory match         |
| 19     | FN_DELETE       | Delete file                         |
| 20     | FN_SEQRD        | Sequential read                     |
| 21     | FN_SEQWRT       | Sequential write                    |
| 22     | FN_CREATE       | Create file                         |
| 23     | FN_RENAME       | Rename file                         |
| 24     | FN_INUSE        | (reserved/stub)                     |
| 25     | FN_CURDRV       | Get current drive                   |
| 26     | FN_SETDMA       | Set DMA (transfer) address          |
| 27     | FN_GETFATPT     | Get FAT pointer                     |
| 28     | FN_WRTPROT      | Write-protect disk (stub)           |
| 29     | FN_GETRDONLY    | Get read-only vector (stub)         |
| 30     | FN_SETATTRIB    | Set file attribute (stub)           |
| 31     | FN_GETDSKPT     | Get drive parameter pointer         |
| 32     | FN_USERCODE     | Get/set user code (stub)            |
| 33     | FN_RNDRD        | Random read                         |
| 34     | FN_RNDWRT       | Random write                        |
| 35     | FN_FILESIZE     | Compute file size in records        |
| 36     | FN_SETRNDREC    | Set random record from sequential   |
| 37     | FN_SETVECT      | Set interrupt vector                |
| 38     | FN_NEWBASE      | Set new program base segment        |
| 39     | FN_BLKRD        | Block read (multiple records)       |
| 40     | FN_BLKWRT       | Block write (multiple records)      |
| 41     | FN_MAKEFCB      | Parse filename into FCB             |

---

## FAT12 encoding  —  `dos86-fat.adb`  —  ASM lines 369-432

Each cluster entry is 12 bits, packed two entries per three bytes.  For cluster N:

```
byte_offset := N + (N / 2)       -- floor(N * 1.5)
word        := Fat(byte_offset)  -- little-endian 16-bit fetch
if N is even:  entry := word and 16#0FFF#
if N is odd:   entry := Shift_Right(word, 4)
```

Special entry values:

| Value         | Meaning                  |
|---------------|--------------------------|
| `16#000#`     | Free cluster             |
| `16#001#`     | Reserved                 |
| `16#002#`–`16#FEF#` | Next cluster in chain |
| `16#FF0#`–`16#FF7#` | Reserved          |
| `16#FF8#`–`16#FFF#` | End-of-file marker |

`Unpack` (ASM 369-395) reads an entry; `Pack` (ASM 402-432) writes one.

---

## Key translation decisions

### 1. Segment registers

86-DOS uses three distinct 64 KB segments simultaneously.  In the Ada translation all
kernel globals collapse into a single `Dos_State` record (the variable `Dos`), with
`-- SEG CS` comments marking accesses that correspond to CS-relative addressing in the ASM.

### 2. Flag-based return conventions

ASM routines return status via CPU flags (carry = error, zero = success, etc.).  In Ada:
- Carry-set paths raise `Dos_Error`.
- Boolean `True`/`False` return values replace non-carry flag tests.
- Pure output values are returned directly by functions or via `out` parameters.

### 3. Case-sensitivity collision

Ada is case-insensitive, so `Fn_Abort`, `FN_ABORT`, and `fn_abort` are identical
identifiers.  The `FN_xxx` constants in `DOS86` clashed with subprogram names in
`DOS86.Syscall`.  Resolution: local dispatch helpers are prefixed `Do_` (`Do_Abort`,
`Do_Version`, etc.) and the dispatch `case` statement uses numeric literals (0..41)
instead of the constant names.

### 4. Reserved words as identifiers

`entry` (Ada task entry) and `rem` (remainder operator) cannot be used as variable names.
Renamed to `Fat_Entry` and `Skip_Rem` in `dos86-fat.adb`.

### 5. Anonymous arrays in records

Ada prohibits anonymous array types as record components.  Named types/subtypes are used:
`DPB_Tab`, `Word_Pair`, `Name_Buf`, `Sec_Buf`, `In_Buf`, `Con_Buf`.

### 6. In-memory FAT

The original ASM places the FAT image immediately after the DPB in memory (`FAT:` label,
ASM line 126).  In Ada the FAT is a fixed-size array field `Fat : Fat_Buffer` inside
`DPB` (where `Fat_Buffer` is `Byte_Array(0 .. MAX_FAT_BYTES - 1)`), avoiding dynamic
allocation.

### 7. SMALLDIR = 1 only

The project translates only the `SMALLDIR=1` conditional-assembly path (16-byte directory
entries).  The `Dirsiz` field in `DPB` still distinguishes small (`0xFF`) from large (`0`)
entries, but no large-entry-specific code is implemented.

---

## Build instructions

```bash
cd msdos-1.0/ada
make          # compile all library sources into build/
make check    # build and run all unit tests
make clean    # remove build/ entirely
```

Requirements: GNAT 14 (`gnatmake` in `PATH`).  No external Ada libraries.

---

## Test results

```
=== build/test_fat ===
PASS  Unpack even cluster 2 -> 3
PASS  Unpack odd  cluster 3 -> 4
PASS  Unpack even cluster 4 -> FFF
PASS  Unpack odd  cluster 5 -> 000
PASS  Pack/Unpack roundtrip even (2->7)
PASS  Pack/Unpack roundtrip odd  (3->5)
PASS  Pack EOF  cluster 4
PASS  Pack FREE cluster 5
PASS  Unpack beyond Maxclus raises Dos_Error
PASS  Fnd_Clus skip=0,1,3 cur/remaining
PASS  Fig_Rec cluster=2 BL=0
PASS  Fig_Rec cluster=3 BL=1
All FAT tests passed.

=== build/test_directory ===
PASS  Lod_Name copy + space-pad (2 variants)
PASS  Get_Bp invalid drive raises Dos_Error
PASS  Get_Bp valid drive does not raise
PASS  Mov_Name NAME1 filled from FCB
All Directory tests passed.

=== build/test_io ===
PASS  Get_Rec Extent=0/1/2 with Nr variants
PASS  Fn_Set_Rnd_Rec Rr encoding (2 variants)
PASS  Fn_Set_DMA DMASEG / DMAADD
All IO tests passed.
```

---

## Implementation phases

| Phase | Files                     | Status       |
|-------|---------------------------|--------------|
|  1    | `dos86.ads / .adb`        | **complete** |
|  2    | `dos86-fat.ads / .adb`    | **complete** |
|  3    | `dos86-disk.ads / .adb`   | **complete** |
|  4    | `dos86-directory.ads / .adb` | **complete** |
|  5    | `dos86-file_ops.ads / .adb`  | **complete** |
|  6    | `dos86-io_ops.ads / .adb` | **complete** |
|  7    | `dos86-console.ads / .adb`| **complete** |
|  8    | `dos86-fcb_util.ads / .adb`| **complete** |
|  9    | `dos86-syscall.ads / .adb`| **complete** |
| 10    | `dos86-init.ads / .adb`   | **complete** |
| 11    | `Makefile`                | **complete** |
| 12    | `tests/test_fat.adb`      | **complete** |
| 13    | `tests/test_directory.adb`| **complete** |
| 14    | `tests/test_io.adb`       | **complete** |

---

## Reference

- Original source: `86DOS.asm` in the parent directory
- Tim Paterson's account of writing DOS:
  <https://web.archive.org/web/20210306031205/http://dosmandrivel.blogspot.com/>
- _The MS-DOS Encyclopedia_, Microsoft Press, 1988
