# ESP32 MS-DOS 1.0 Port — Design (revised: emulated kernel)

**Date:** 2026-05-04 — revised after pivoting away from a translated kernel
**Status:** Proposed (awaiting user review)
**Prior attempt:** `archive/plan1-translation-attempt` git tag preserves the
abandoned approach (jgbarah's C translation) and the kernel-flow bugs that
killed it.

## Why this revision

The first design tried to run the DOS kernel as native Xtensa code via
jgbarah's C translation of `86DOS.ASM`. Initial verification surfaced
multiple translation bugs in core file-I/O paths (`io_store` BYTCNT1 reuse,
`fat_fndclus` return-discard, `directory.c::contsrch` SI offset). Patching
them is days of careful ASM-vs-C audit, and the next round of tests
(delete / rename / filesize) almost certainly hides similar issues.

The pivot: **don't translate the kernel; emulate it.** Take Tim Paterson's
actual `86DOS.ASM` source from
`Paterson-Listings/3_source_code/86-DOS_1.00/`, assemble it with a modern
assembler, and run the resulting 8086 binary on a software 8086 emulator on
ESP32. The kernel stays byte-identical to what shipped in 1981.
Translation bugs become impossible by construction.

## Summary

A plain ESP32-WROOM-32 boots to a DOS prompt in your browser, running
unmodified MS-DOS 1.0 source code on a software 8086. All DOS code
(kernel + a small assembly shell we write + any `.COM` programs) executes
inside the emulator. The ESP32 provides what the IBM PC's BIOS provided in
1981: console I/O (routed via WebSocket to an xterm.js terminal page over
WiFi), disk I/O (routed to a FAT12 image baked into the ESP32's flash), and
a clock.

### What's novel

Tim Paterson released his original 86-DOS source listings publicly in early
2025 (`DOS-History/Paterson-Listings`). **Building MS-DOS 1.0 *from* those
listings — assembling the original 1981 source ourselves, no pre-built
binary — and running it on a $5 microcontroller with WiFi-served browser as
I/O** appears to be unprecedented as a documented project. Plenty of
projects emulate IBM PC hardware and boot pre-built DOS images on ESP32
(FabGL et al.); none, to our knowledge, build the kernel from these
listings.

## Goals

- Boot the assembled `86DOS.ASM` on plain ESP32-WROOM-32 (no PSRAM)
- Provide BIOS services (console + disk + clock) such that the kernel boots
  cleanly to its first transient program — a DOS prompt
- Browser-based terminal as the user-facing interface (over WiFi)
- 320 KB FAT12 disk image baked into ESP32 flash
- A minimal `SHELL.COM` (~100–200 lines of 8086 assembly we write) drives
  the prompt, parses input, loads and runs other `.COM` files
- Run at least one period-correct `.COM` (likely `EDLIN`, possibly `DEBUG`)
  end-to-end

## Non-goals (v1)

- BASIC / BASICA (depend on IBM PC ROM BASIC)
- VGA/HDMI video, PS/2 or USB keyboard
- `.EXE` files (DOS 2.0+ feature)
- `FORMAT`, `DISKCOPY`, `DISKCOMP` (need block-device hooks beyond v1)
- Multitasking, networking from inside DOS, TSRs
- Captive-portal WiFi provisioning (compile-time creds for v1)

## Architecture

Two FreeRTOS tasks share two byte ring buffers:

```
  +--------------------------+         +-------------------------+
  |         net_task         |         |        dos_task         |
  |                          |         |                         |
  |  WiFi + lwIP             |         |  8086 emulator main     |
  |  HTTP server (terminal   |         |    loop                 |
  |     page)                |         |    └─ on far-call to    |
  |  WebSocket /tty endpoint |         |       BIOSSEG: trap     |
  |                          |         |       └─ BIOS handler   |
  |                          |         |          (console/disk) |
  +-----+--------------+-----+         +----+-------------+------+
        |              ^                    |             ^
        v              |                    v             |
     [output ring] <----                  -----------> [input ring]
```

`net_task` is intentionally ignorant of DOS — it shuttles bytes between the
WebSocket and the two ring buffers.

Inside `dos_task`, top-to-bottom:

1. **8086 emulator core** — real-mode 16-bit instruction interpreter.
   Allocates ~96 KB of 8086 address space (kernel + one user segment +
   scratch). Adapted from Adrian Cable's `8086tiny`, with built-in
   IBM-PC-BIOS / VGA / timer / DMA / disk shims stripped or replaced.
2. **BIOS trap dispatcher** — when the emulator hits a far CALL whose
   target segment is `BIOSSEG` (0x0040, per `86DOS.asm:130-143`), the
   dispatcher reads the offset, identifies which BIOS entry the kernel
   was calling (BIOSSTAT / BIOSIN / BIOSOUT / BIOSPRINT / BIOSAUXIN /
   BIOSAUXOUT / BIOSREAD / BIOSWRITE / BIOSDSKCHG), pulls AL/BX/CX/DX
   from the emulator's CPU state, calls the matching ESP32 handler,
   writes results + carry flag back, simulates RETF.
3. **BIOS handlers** — three small files: `bios_console.c`,
   `bios_disk.c`, `bios_clock.c`. Console handlers move bytes to/from the
   ring buffers; disk handlers call the flash block device.
4. **Flash block device** — FAT12 partition exposed as 512-byte sectors,
   with a 4 KB RMW cache for ESP32's erase granularity.

The kernel itself runs *above* all this, unchanged. When user `.COM` code
hits `INT 21h`, the kernel's own ASM dispatcher handles it inside the
emulator — we don't intercept that path. We only intercept BIOS far-calls.

## Components

### `emulator_8086`
- Real-mode 8086 instruction interpreter
- ~96 KB allocated address space (a single contiguous host buffer)
- Hook: `on_far_call(seg, offset)` — if `seg == BIOSSEG`, dispatch BIOS;
  else proceed normally
- State: 14 16-bit registers, flags, the memory buffer
- Halts on `HLT` or unhandled trap

### `bios_dispatch`
- Knows the layout of the BIOS jump-stub area at `BIOSSEG:0` (each entry
  is 3 bytes; offsets 0/3/6/9/12/15/18/21/24/27 per `86DOS.asm:130-143`)
- Maps offset → handler via small lookup
- Handles register-marshalling: read AL/BX/CX/DX from the emulator's
  CPU state, call C handler, write back AL/AH/CF as the entry's
  conventions require (see ASM lines 130-143)

### `bios_console`
- `bios_stat()` → AL: 0 if input ring empty, non-zero otherwise
- `bios_in()` → AL: blocks (yields task) until a byte is available
- `bios_out(ch)` — push AL to output ring
- `bios_print(ch)` — stub (no printer in v1)
- `bios_auxin/auxout` — stubs

### `bios_disk`
- `bios_read(drive, buf_seg, buf_off, count, sector)` — resolve
  buf_seg:buf_off into a host pointer into the emulator's memory; call
  `flash_disk_read`; set/clear carry as success/error
- `bios_write(...)` — symmetric
- `bios_dskchg(drive)` — return AH=1 (not changed)

### `flash_disk`
- 320 KB FAT12 partition exposed as 512-byte sectors
- 4 KB RMW: load the parent 4 KB block, modify, queue erase + write on
  flush or eviction

### Kernel binary (`DOS.SYS`)
- Source: `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` —
  unchanged, in-place
- Assembled with NASM (or `yasm`) producing a flat binary
- Likely needs a tiny `kernel.patches` directory of mechanical
  syntax-only adjustments where SCP-ASM 2.43 conventions don't match
  NASM. Adjustments must be syntactic only — never logic. Each tagged
  with `; PATCH (NASM): <reason>` comment for grep
- Loaded by the emulator at the segment the kernel expects

### `SHELL.COM`
- Source: `shell/shell.asm` — ~100–200 lines we write
- Behavior:
  - Print prompt (`A>`)
  - `INT 21h AH=0Ah` — buffered line input
  - Parse first whitespace-separated token as command
  - Built-ins: `DIR`, `TYPE <name>`, possibly `DEL`, `REN`, `EXIT`
  - External: open `<cmd>.COM` via FCB, read it via SEQRD into a fresh
    user segment at offset 0x100, build a minimal PSP at offset 0x00,
    `JMP FAR` to user_seg:0x100
  - On user program return (`INT 20h` or `AH=00h`): back to prompt
- Period-authentic 8.3 filename UX
- Lives on the disk image as `SHELL.COM`

### Disk image layout
- 320 KB FAT12, geometry `SECSIZ=512, SPC=2, FIRFAT=1, FATCNT=2,
  MAXENT=112, DSKSIZ=640`, media descriptor 0xFF (DOS 1.0 5.25" DS)
- Sector 0: boot sector (zero — emulator loads kernel directly into RAM,
  not via boot sector)
- Sectors 1–4: FATs
- Sectors 5–11: root directory
- Data: contains `SHELL.COM`, optionally `EDLIN.COM` / `DEBUG.COM`, user
  files

### Disk image authoring
- Small Python helper (`tools/build_disk.py`) takes a directory of files,
  builds a 320 KB FAT12 image with the geometry above, writes to
  `disk.img`. Flashed to the `dos_disk` partition via
  `esptool.py write_flash <offset> disk.img`.

## Boot sequence

1. ESP32 boots → ESP-IDF init → `net_task` and `dos_task` start
2. `dos_task`: emulator allocates memory buffer, loads kernel binary at
   the kernel's expected segment, sets CS:IP to kernel entry point
3. Emulator runs. Kernel calls `BIOSIN` for date prompt — dispatcher
   routes to `bios_in` which blocks on input ring; user types date in
   browser; kernel parses, prints banner, sets `ENDMEM`, then `JMP FAR`
   to load the first transient program
4. Kernel transient-loader opens `SHELL.COM` from disk via its own
   internal FCB plumbing → `BIOSREAD` traps to flash; SHELL.COM is
   loaded into a user segment at 0x100
5. Kernel jumps to SHELL.COM, which prints `A>` and waits for input
6. Steady state: SHELL.COM line-edits via INT 21h → kernel
   handles INT 21h in-emulator, calling BIOSIN/BIOSOUT for terminal I/O
   and BIOSREAD/BIOSWRITE for disk

## Memory budget (plain ESP32, no PSRAM)

Approximate usable DRAM after ESP-IDF + WiFi: ~280–320 KB.

| Component                      | Size       |
|--------------------------------|------------|
| 8086 address space (host buf)  | 96 KB      |
| Emulator state (registers etc) | <1 KB      |
| Flash 4 KB RMW buffer          | 4 KB       |
| Console rings (input+output)   | 4 KB       |
| WebSocket recv frame           | 8 KB       |
| `dos_task` stack               | 16 KB      |
| `net_task` stack               | 8 KB       |
| WiFi/lwIP runtime              | ~80 KB     |
| HTTP server + buffers          | ~12 KB     |
| **Total**                      | **~228 KB** |

Slack: ~50–90 KB. Comfortable for v1.

## Flash layout (4 MB part)

| Partition     | Type                      | Size              |
|---------------|---------------------------|-------------------|
| bootloader    | (system)                  | ~64 KB            |
| partition tbl | (system)                  | 4 KB              |
| nvs           | data                      | 24 KB             |
| phy_init      | data                      | 4 KB              |
| factory app   | app                       | up to 1.5 MB      |
| **dos_disk**  | data (custom subtype)     | 320 KB            |
| (free)        | —                         | remainder of 4 MB |

## Build & validation strategy

Three build targets:

- **`asm/`** — assembles `86DOS.ASM` (+ any kernel patches, if needed) and
  `shell.asm` to flat binaries with NASM
- **`host/`** — compiles the 8086 emulator + a stdio BIOS + a file-backed
  disk image. Iteration target for emulator and BIOS work without
  flashing. Runs on Windows.
- **`esp32/`** — ESP-IDF project. Same emulator + BIOS handler sources;
  different I/O backends (flash, WebSocket).

The kernel binary is built once and reused across host and ESP32 targets.

The host target unblocks a critical workflow: getting the kernel to boot
and execute the date prompt cleanly inside the emulator on Windows BEFORE
we touch ESP32 toolchain. Once the kernel boots happily on host, ESP32 is
just plumbing.

## Risks & open questions

1. **8086 emulator correctness.** The kernel exercises a substantial
   fraction of the 8086 ISA. `8086tiny` is well-trodden but compact.
   *Mitigation:* host target lets us debug emulator vs kernel quickly.
   If a kernel instruction misbehaves, we can compare against
   reference emulators (DOSBox source, Fake86) and fix the decoder.

2. **`86DOS.ASM` assembling cleanly with NASM.** The original was written
   for Tim Paterson's SCP ASM 2.43 (also in the listings repo). Some
   directives may need syntax tweaks for NASM. *Mitigation:* try NASM
   first; if too painful, build SCP ASM 2.43 itself (also from the
   listings) and use it. Either way, kernel logic is not modified.

3. **Kernel `MEMSCAN`.** `dos_init` probes RAM by write-verify-pattern
   to find the top of memory. If we allocate 96 KB, `MEMSCAN` might
   walk past the end. *Mitigation:* one-line ASM patch tagged
   `; PATCH (HOST): cap MEMSCAN at <addr>` if the original walks past
   our buffer.

4. **Shell completeness.** `SHELL.COM` needs enough COMMAND.COM-shaped
   behavior to load `.COM` files. Loading involves PSP construction and
   FAR JMP. Finicky but well-documented.

5. **Period-correct `.COM` binaries.** EDLIN/DEBUG must come from public
   archives that match DOS 1.0 syscall expectations. Most are widely
   circulated; verify before relying.

6. **Memory budget assumes WiFi runtime ~80 KB.** Will measure on first
   WiFi-enabled build.

7. **Re-syncing with upstream Paterson-Listings.** If Tim Paterson updates
   the listings, we re-pull and reassemble. Our `kernel.patches/` (if
   any) should hold *only* assembler-syntax changes — never logic — so a
   re-sync is a straightforward re-apply.

## Roadmap beyond v1

- WiFi provisioning UI (captive portal)
- SD card alternative storage
- SNTP-driven date/time
- More period-correct utilities verified end-to-end
- `.EXE` support (would need DOS 2.0+ kernel — separate project)
