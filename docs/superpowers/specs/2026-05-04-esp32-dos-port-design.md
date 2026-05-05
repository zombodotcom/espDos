# ESP32 MS-DOS 1.0 Port — Design

**Date:** 2026-05-04
**Status:** Proposed (awaiting user review)

## Summary

Port MS-DOS 1.0 to a plain ESP32 (WROOM-32). The DOS kernel runs as native
Xtensa code via jgbarah's C translation of `86DOS.asm`. Unmodified DOS-1.0-era
`.COM` programs run on top of it through a small 8086 user-mode emulator that
traps `INT 21h` into the native kernel. The console is exposed over WiFi to a
browser-based terminal — no VGA hardware, no PS/2 keyboard.

Demo target: a user opens a webpage served by the ESP32, types `EDLIN
HELLO.TXT`, and the actual 1981 `EDLIN.COM` binary edits a file on a FAT12
filesystem stored in the ESP32's flash.

## Goals

- Boot a working DOS 1.0 environment on plain ESP32-WROOM-32 (no PSRAM)
- Run unmodified DOS-1.0-era `.COM` binaries against a real FAT12 filesystem
- Browser-based terminal as the user-facing interface (over WiFi)
- Disk image baked into ESP32 flash (no SD card hardware required)

## Non-goals (v1)

- BASIC / BASICA — depends on IBM PC ROM BASIC and BIOS interrupts beyond INT 21h
- VGA/HDMI video output, PS/2 or USB keyboard
- `.EXE` files (DOS 1.0 didn't have them; they came in 2.0)
- `FORMAT`, `DISKCOPY`, `DISKCOMP` (need block-device hooks we won't expose)
- Multitasking, networking from inside DOS programs, TSRs

## Architecture

Two FreeRTOS tasks share two byte ring buffers:

```
  +--------------------------+         +-------------------------+
  |         net_task         |         |        dos_task         |
  |                          |         |                         |
  |  WiFi + lwIP             |         |  Native shell           |
  |  HTTP server (terminal   |         |    └─ EXEC loader       |
  |     page)                |         |        └─ 8086 emulator |
  |  WebSocket /tty endpoint |         |            └─ INT 21h   |
  |                          |         |                dispatch |
  |                          |         |                 └─ DOS  |
  |                          |         |                  kernel |
  |                          |         |                   └─ BIOS|
  |                          |         |                    vtable|
  +-----+--------------+-----+         +----+-------------+------+
        |              ^                    |             ^
        v              |                    v             |
     [output ring] <----                  -----------> [input ring]
```

`net_task` is intentionally ignorant of DOS — it shuttles bytes between the
WebSocket and the two ring buffers. This isolates the WiFi stack from the
deterministic DOS world.

Inside `dos_task`, layered top-to-bottom:

1. **Native shell** — replaces `COMMAND.COM`. Built-ins: `DIR`, `TYPE`, `COPY`,
   `REN`, `DEL`, `CLS`, `DATE`, `TIME`. Non-built-ins → EXEC loader.
2. **EXEC loader** — opens `<name>.COM` via DOS file syscalls, copies bytes
   into the emulator's user segment at offset `0x100`, builds a minimal
   Program Segment Prefix (PSP) at `0x000`, sets `CS=DS=ES=SS=user_seg`,
   `IP=0x100`, `SP=0xFFFE`, runs the emulator until termination.
3. **8086 emulator** — adapted from Adrian Cable's `8086tiny`. Real-mode,
   single 64KB segment, no protected mode, no segmentation tricks. The
   emulator's only "interrupt" path is the INT 20h / INT 21h trap.
4. **INT 21h dispatcher** — pulls AH/AL/BX/CX/DX/DS/ES out of the emulator
   state, calls the matching native kernel `fn_*`, writes return values and
   carry flag back. INT 20h sets a halt flag.
5. **DOS kernel** — jgbarah's C translation, vendored verbatim under
   `vendor/msdos-1.0/c/`. Already abstracts the BIOS behind a `bios_vtable_t`.
6. **BIOS vtable (our impl)** — three concerns: console (routes to ring
   buffers), disk (routes to flash block device), clock (`esp_timer`).
7. **Flash block device** — exposes a 1.44 MB partition as 512-byte sectors.
   Handles ESP32 flash's 4 KB erase granularity with a 1-block RMW cache.

## Components

### `bios_esp32` — BIOS vtable implementation

Implements the nine function pointers `bios_vtable_t` requires:

- `stat()` — non-zero if input ring has bytes
- `in()` — read one byte from input ring; yields `dos_task` while empty
- `out(ch)` — push one byte to output ring
- `print(ch)` — stub (no printer)
- `auxin/auxout` — stub
- `read(drive, buf, count, sector)` / `write(...)` — call into `flash_disk`
- `dskchg(drive)` — return 1 (not changed); flash is fixed media

### `flash_disk` — block device

- API: `disk_read(byte *buf, uint32_t sector, uint32_t count)`,
  `disk_write(...)`. 512-byte sectors.
- Backed by an ESP32 flash partition named `dos_disk` (custom partition type),
  sized for a **DOS 1.0 320 KB double-sided floppy**:
  `SECSIZ=512, SPC=2, FIRFAT=1, FATCNT=2, MAXENT=112, DSKSIZ=640`. This is
  period-authentic, fits comfortably in flash, and matches geometry the
  kernel translation has been exercised against.
- 4 KB-aware: maintains a single 4 KB read-modify-write buffer. Sub-4 KB
  writes load the parent 4 KB block, modify it, and queue an erase + write
  on flush or eviction.
- Mounting: at boot, locate the partition by name.

### `emulator_8086` — instruction interpreter

- Source: adapted from Adrian Cable's `8086tiny` (BSD-licensed, ~25 KB of
  readable C). Strip its built-in BIOS, VGA, timer, DMA, and disk shims —
  the only "interrupt" reaching our emulator is INT 21h via DOS programs and
  INT 20h for termination.
- State: 14 16-bit registers + 64 KB user segment buffer. We allocate one
  fixed buffer; `.COM` files use a single segment by definition.
- Hook: `void on_int(uint8_t vector, cpu_state_t *cpu)`. INT 21h flows to
  the dispatcher; INT 20h sets a halt flag.

### `int21_dispatch` — DOS syscall bridge

- `void dispatch(cpu_state_t *cpu)` reads AH from the cpu state, switches on
  it, calls the matching kernel `fn_*` with arguments translated from the
  appropriate registers and segment+offset pairs into native pointers
  (offsets are relative to the emulator's user segment buffer).
- Writes return values back: AL / carry flag / DX / BX as appropriate per
  DOS 1.0 conventions.
- DOS 1.0 syscall set is small (~40 functions); roughly 200 lines of switch
  + register marshalling.

### `native_shell` — replaces COMMAND.COM

- Single line-input loop. Reads via DOS `FN_BUFIN`, parses, dispatches.
- Built-ins (`DIR`, `TYPE`, `COPY`, etc.) call the kernel directly — no
  emulator round-trip for trivial commands.
- Non-built-in: try `<cmd>.COM` on current drive via EXEC loader; on
  failure, "Bad command or file name."

### `web_terminal` — browser front end

- Static asset: one HTML file with `xterm.js` (bundled into firmware).
- Browser opens WebSocket to `ws://<esp32-ip>/tty`; raw bytes both
  directions.
- Server: ESP-IDF's built-in `esp_http_server` + `httpd_ws_*` APIs.

### `provisioning` — WiFi credentials

- v1: WiFi credentials compiled into firmware via `idf.py menuconfig`
  (`CONFIG_WIFI_SSID` / `CONFIG_WIFI_PASS`).
- Captive-portal fallback for unconfigured devices is deferred to v2.

## Data flow: typical command

User types `EDLIN HELLO.TXT\n` in the browser:

1. Browser → WebSocket → `net_task` → input ring buffer
2. `dos_task` is blocked in `fn_bufin()` → kernel calls `BIOSIN()` →
   `bios_esp32::in()` → pops from input ring buffer (yields task while empty)
3. Native shell receives the line → parses → command=`EDLIN`, args=`HELLO.TXT`
4. Not a built-in → EXEC loader looks up `EDLIN.COM` via kernel file
   syscalls (`fn_open`, `fn_seqrd` loop, `fn_close`)
5. Loader copies file bytes into emulator user segment at `0x100`, fills
   PSP at `0x00` (command tail at offset `0x80`)
6. Loader sets emulator registers, runs until halt
7. EDLIN executes 8086 instructions; first INT 21h trap hits dispatcher
8. Dispatcher reads AH=09h (print string), looks up DS:DX in user segment,
   calls `fn_conout` for each byte until `$` terminator
9. Each `fn_conout` → `con_out` → `BIOSOUT` → `bios_esp32::out` → output
   ring → `net_task` → WebSocket → browser displays characters
10. Eventually EDLIN does INT 20h or AH=00h → emulator halts → control
    returns to native shell → prompt printed

## Memory budget (plain ESP32, no PSRAM)

Approximate usable DRAM heap after ESP-IDF boot with WiFi: ~280–320 KB.

| Component                     | Size        |
|-------------------------------|-------------|
| 8086 user segment             | 64 KB       |
| DOS kernel state + DPBs       | ~6 KB       |
| Sector buffers (kernel)       | 1 KB        |
| Directory buffer (kernel)     | 0.5 KB      |
| FAT cache                     | ~1 KB       |
| Flash 4 KB RMW buffer         | 4 KB        |
| Console rings (input+output)  | 4 KB        |
| WebSocket recv frame          | 8 KB        |
| `dos_task` stack              | 16 KB       |
| `net_task` stack              | 8 KB        |
| WiFi/lwIP runtime             | ~80 KB      |
| HTTP server + buffers         | ~12 KB      |
| **Total**                     | **~205 KB** |

Leaves ~75–115 KB headroom. Tight but workable. First lever if we overrun:
shrink WebSocket recv frame.

## Flash layout (4 MB part)

ESP-IDF default partitions plus our custom one. Exact offsets are computed by
ESP-IDF from `partitions.csv`; the relevant addition is the `dos_disk` entry:

| Partition       | Type     | Size              |
|-----------------|----------|-------------------|
| bootloader      | (system) | ~64 KB            |
| partition table | (system) | 4 KB              |
| nvs             | data     | 24 KB             |
| phy_init        | data     | 4 KB              |
| factory app     | app      | up to 1.5 MB      |
| **dos_disk**    | **data (custom subtype)** | **320 KB (327,680 B)** |
| (free)          | —        | remainder of 4 MB |

Disk image is authored on host. A small Python helper takes a directory of
files, builds a 320 KB FAT12 image with the geometry our `flash_disk`
exposes, then flashes it via `esptool.py write_flash <offset> disk.img`.

## Build & host validation strategy

Two coexisting build targets share the kernel sources:

- **`host` build** — compiles the kernel + a stdio-backed BIOS vtable +
  a file-backed disk image. Runs on Windows/Linux. Used for unit tests and
  for any kernel-touching change. Sub-second feedback loop. Already
  scaffolded under `host/main.c` and proven to boot the kernel.
- **`esp32` build** — ESP-IDF project under `esp32/`. Same kernel sources;
  different BIOS vtable + drivers + WebSocket terminal.

Kernel changes are validated on host before being flashed to ESP32.
Hardware-touching changes (block device, BIOS implementation, terminal) are
flashed and tested on device.

## Risks & open questions

1. **jgbarah's translation may have latent bugs.** The spike confirmed
   `dos_init()` works; file ops are unverified. *Mitigation:* extend host
   smoke test to cover create / write / read / dir-walk / delete / rename
   before relying on the kernel for harder workloads.
2. **Memory budget assumes WiFi runtime ~80 KB.** Real number could be
   higher; we measure on first WiFi-enabled build. *Mitigation:* the
   WebSocket recv frame and the 4 KB RMW buffer are both adjustable.
3. **`8086tiny` extraction effort is unknown.** We need to confirm we can
   isolate the instruction decoder from its built-in BIOS shims without
   leaving a tangle. *Mitigation:* if the extraction is messy, fall back to
   a minimal hand-rolled user-mode emulator (~2000 lines).
4. **DOS 1.0 binary availability.** EDLIN, DEBUG, COMP must come from
   public archives that match this kernel's expectations. The
   Paterson-Listings repo has the kernel source only, not the utilities.
   *Mitigation:* identify at least one binary (most likely EDLIN) we can
   confirm runs to spec before declaring v1 done.
5. **If the plain-ESP32 budget overruns.** S3 with PSRAM is the documented
   fallback. We do not retarget proactively; we measure first.
6. **Date prompt is unconditional at boot.** DOS 1.0 prompts for date every
   boot; this is authentic but may surprise users. *Mitigation:* document
   it; v2 may auto-fill via SNTP.

## Roadmap beyond v1

- WiFi provisioning UI / captive portal
- Optional SD card support as alternative storage
- SNTP-driven automatic date/time
- More DOS utilities verified end-to-end (DEBUG, COMP, custom user `.COM`)
