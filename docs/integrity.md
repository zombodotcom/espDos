# espDos integrity: what is Paterson's, what is ours, and why this is honest

The pitch is "Tim Paterson's actual 86-DOS source code running on a $5 chip."
This document is the honest accounting of that claim. It enumerates every
place the running system departs from a strict reading of the pitch and
explains why each departure is defensible. A skeptical reader should be
able to look at any sentence here and verify it from the codebase in under
a minute.

The summary, in one paragraph: the kernel binary that runs on the ESP32-S3
is produced from `86DOS.ASM` (Paterson, 1981) by mechanical syntax
translations only — the original file is read-only and we never touch it.
The 8086 instruction interpreter that runs that binary is our fork of
Adrian Cable's 8086tiny (MIT-licensed). The IBM-PC-shaped BIOS that the
kernel calls into, the boot stub, and the disk image around the kernel are
ours, and are written to match the contracts the kernel expects rather
than to alter the kernel's behavior.

There is no COMMAND.COM in v1. The system boots, runs `DOSINIT`, scans
memory, and prints the 86-DOS banner. After that there is no shell to
read commands at the prompt. That limitation is called out in section 5.

---

## 1. What is Paterson's, byte-for-byte

The original kernel source is `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM`,
3621 lines, dated 04/28/81 in its own header (line 1). It is **read-only**
in this repository — nothing in the build pipeline writes to it.

The build pipeline is:

```
86DOS.ASM
   |
   v
asm/scp_to_nasm.py     (mechanical SCP-ASM -> NASM translation)
   |
   v
build/kernel.translated.asm   (88,625 bytes of NASM-syntax assembly)
   |
   v
nasm -f bin
   |
   v
build/kernel.bin       (6,341 bytes — the 8086 binary actually executed)
```

`build/kernel.bin` is loaded unmodified into emulator memory at segment
`0x0100`, offset `0x0100` (see `firmware/main/espdos.c` lines 130-133, and
`firmware/components/esp8086/include/esp8086.h` lines 39-40, which encode
the same constants).

The reference 86-DOS kernel under SCP-ASM 2.43 was approximately 5,861
bytes (the comment in `firmware/main/espdos.c` line 51 records the
expected baseline). Our `kernel.bin` is 6,341 bytes — about 9% larger —
because R13b (section 2) expands every conditional jump to a 5-byte
sequence to keep the binary on 8086-legal opcodes only. Control flow is
preserved label-for-label.

There is exactly one entry point: `JMP DOSINIT` at line 168 of
`86DOS.ASM`. Everything that runs runs from that source line.

## 2. The 22 SCP -> NASM translation rules

`asm/scp_to_nasm.py` (623 lines) implements every translation. Each rule
is documented in source comments alongside the regex that recognizes it.
The full list:

| ID  | Rule                                                                                 |
|-----|--------------------------------------------------------------------------------------|
| R1  | 4-letter string-op shortcuts (`LODB`, `STOW`, `MOVB`, `CMPW`, `SCAB`, ...) -> NASM operandless string ops |
| R2  | Direction-flag shortcuts: `DOWN` -> `std`, `UP` -> `cld` (also `DI`/`EI` -> `cli`/`sti`) |
| R3  | `SEG <reg>` standalone segment override -> fold `reg:` into the next memory operand, or emit raw prefix byte if there is no `[mem]` to fold into |
| R4  | Unary `SHL R` / `SHR R` etc. default to count 1 -> NASM requires explicit `, 1`     |
| R5  | Two-operand `DIV/MUL/IMUL AX, src` -> single-operand NASM form, with byte/word size hint added if `src` is memory |
| R6  | `DS n` (Define Storage) -> `resb n` in struct sections, `times n db 0` in code sections |
| R7  | Multiple `ORG 0` at top of file are field layouts (FCB / DPB / BIOSSEG / register-save) -> `absolute 0` blocks. First `ORG 0` followed by `PUT NNN` switches to code at origin NNN. |
| R8  | `IF expr` / `ENDIF` -> `%if expr` / `%endif`                                         |
| R9  | `JP <label>` -> `jmp <label>` (SCP shorthand for unconditional jump; NASM's `JP` means jump-if-parity, which is not what 86DOS.ASM means) |
| R10 | Standalone `ALIGN` -> `align 2`                                                      |
| R11 | Rename labels colliding with NASM keywords (`DEFAULT` -> `SCPDEFAULT`)              |
| R12 | `SBC dst, src` -> `sbb dst, src`                                                    |
| R13 | `J<cond> RET` (conditional return) -> invert + inline `ret` skip pattern            |
| **R13b** | **Every `Jcc <label>` -> `Jnotcc .skip; JMP <label>; .skip:` (see below)**       |
| R14 | `<op> B, [mem]` / `<op> W, [mem]` size hints -> inline `byte`/`word`                 |
| R15 | Bare `PUSH [mem]` / `POP [mem]` need explicit `word` in NASM                         |
| R16 | `CALL <offset>, <segment>` / `JMP <offset>, <segment>` (offset-first far call) -> NASM `call seg:offset` |
| R17 | `JMP L, [mem]` (far indirect) -> `jmp far [mem]`                                     |
| R18 | `MOV [mem], imm` with no size -> add `word` (only if source is not a register)      |
| R19 | `JCXZ RET` -> two-step skip pattern (JCXZ has no inverse)                            |
| **R19b** | **`JCXZ <label>` -> long-form skip-trampoline (same reason as R13b)**           |
| R20 | `RET L` -> `retf` (far return)                                                       |
| R21 | `INC [mem]` / `DEC [mem]` (no size) -> add `word`                                    |
| R22 | `CMP [mem], imm` (non-register source) -> add `word`                                 |

### Why R13b deserves its own callout

The 8086 only supports `Jcc rel8`: a 1-byte signed offset, +/-127 bytes
from the next instruction. NASM at its default CPU level (>= 386)
silently upgrades any out-of-range `Jcc rel8` to the 80386+
`0F 8x rel16` two-byte-opcode form. Adrian Cable's 8086tiny decoder does
not support `0F 8x` — it consumes `0F` as `POP CS` (a 8086-only opcode
that was reused as `POP CS` in real 8086 hardware) and falls through to
garbage on the rest, which corrupts emulator memory.

The smoking gun, recorded in the comment around `_RE_JCC_LABEL` in
`asm/scp_to_nasm.py` (lines 156-178): `JB CTRLOUT` at file offset
`0x102D` had a target 131 bytes away — just past `rel8` range.
NASM emitted `0F 82 81 00` (4 bytes), which the 8086tiny decoder
mis-decoded as `ADD WORD [BX+SI], 0x7F3C`, scribbling random data into
the `DATMES` data area.

The fix is structural, not local: every `Jcc target` is rewritten as

```
    Jnotcc .skip
    JMP    target
.skip:
```

This always uses 8086-legal opcodes (`Jcc rel8` to a label always within
range, plus `JMP rel16`). It works regardless of distance.

**Cost:** kernel grows from approximately 5,861 to 6,341 bytes (around
8%). This is the single largest source-level intervention in the
preprocessor. The output is still a deterministic transform of the
original — a label-for-label rewrite, no semantic edits — but a reader
who unwraps the pipeline should know that not every byte in the assembled
kernel is a byte SCP-ASM 2.43 would have produced. The instruction
*sequence at every address is something the 8086 can execute*, the
control flow lands on the same labels Paterson named, and the visible
behavior is identical.

R19b is the sister rule for `JCXZ`, which has no inverse mnemonic and
needs its own slightly-different trampoline shape (see lines 511-529 of
`scp_to_nasm.py`).

### Rule ordering matters

Two of the rules above must run before R13/R13b or the kernel
silently breaks in ways that only surface long after MEMSCAN:

- **R9** (`JP <label>` -> `jmp <label>`) must run **before** R13/R13b.
  In SCP-ASM, `JP` is shorthand for an unconditional `JMP`. NASM
  reads `JP` as "jump if parity," so R9 rewrites it to `jmp`. If
  R13/R13b run first, they see what looks like a conditional jump
  and expand it into a parity-conditional skip-trampoline. The
  resulting kernel assembles without errors but takes the wrong
  branch in the date parser (`MYD`) and in `BUFIN`'s character-
  echo loop, so the date prompt becomes a dead end. We hit exactly
  this in development; the fix was to move R9 above R13/R13b in the
  rule pipeline.

The general principle: **synonym/rename rules (R9, R12, R20) run
before structural-rewrite rules (R13, R13b, R19, R19b)** so the
structural rewrites only fire on instructions that actually have
the semantics those rewrites assume. This is documented in the
ordering of the `RULES` list at the top of `scp_to_nasm.py`.

## 3. What we wrote

Four substantive pieces of code surround the kernel. Sizes are
line-counted at the time of writing.

### `asm/bootstub.asm` — 117 lines

Plays the SCP boot ROM / IO.SYS role. The kernel cannot start cold: it
expects `DS:SI` to point at a Drive Parameter Block init table on its
very first instruction (line 3301 of `86DOS.ASM`: `LODB` reads
`NUMDRV`), and it expects the exit-class IVT vectors (20h, 22h, 23h,
24h, 27h) to already be valid before any program terminates.

Three jobs:

1. Install IVT entries 20h/22h/23h/24h/27h pointing at our `halt:` label
   (a `hlt; jmp halt` loop). Without this, the kernel's first INT 22h
   reads four uninitialized bytes from low memory as a CS:IP target and
   walks off into junk.
2. Set `DS:SI` to a static DPB init table: `db 1` (NUMDRV = 1 drive),
   followed by the offset of one DPT, followed by the DPT itself
   (SECSIZ=512, SEC_PER_CLUS=1, FIRFAT=1, FATCNT=2, MAXENT=64,
   DSKSIZ=720). The format and field order are derived from `PERDRV` in
   `86DOS.ASM` lines 3306-3389.
3. `JMP FAR KERNEL_SEG:KERNEL_OFFSET` to hand control to the kernel.

Build artifact: `build/bootstub.bin`, 74 bytes. The
`KERNEL_SEG`/`KERNEL_OFFSET` constants are taken from
`firmware/components/esp8086/include/esp8086.h` at build time (extracted
by `asm/build_kernel.sh` lines 47-57) so there is exactly one source of
truth for those addresses.

### `firmware/components/esp8086/esp8086.c` — 928 lines

This is our fork of Adrian Cable's 8086tiny v1.25 (MIT-licensed). The
upstream source is preserved at `third_party/8086tiny/8086tiny.c` (774
lines) for direct comparison. Lineage is credited at the top of
`esp8086.c` (lines 1-26). The fork is also MIT-licensed.

Three structural changes vs. upstream:

1. **Memory size: 1 MB + 64 KB margin** (`RAM_SIZE = 0x110000`,
   `esp8086.c` line 89). Upstream allocates exactly 1 MB; we add 64 KB
   so that the natural seg+off overflow real-mode 8086 code does — the
   maximum address from `seg:off` is `0xFFFF*16 + 0xFFFF = 0x10FFEF` —
   does not need per-access masking. The `mem[]` array is decorated with
   `EXT_RAM_BSS_ATTR` on `ESP_PLATFORM` so the firmware build places it
   in PSRAM; host builds get plain BSS, which is fine on x86.
2. **Char-signedness independence.** Every cast that needed `signed
   char` sign-extension is rewritten as `(int8_t)`. This lets the file
   build correctly under both `-fsigned-char` (x86 host) and the
   default-unsigned-char Xtensa-ESP-ELF toolchain — no `-fsigned-char`
   compiler crutch needed.
3. **BIOSSEG far-call trap.** At the top of the run loop
   (`emu_run_n()`, `esp8086.c` lines 349-372), if `regs16[REG_CS] ==
   0x0040`, we do *not* execute whatever bytes happen to be at that
   address. We dispatch to `bios_handle_call(reg_ip)` and synthesize a
   `RETF` (pop IP, pop CS, SP += 4). The bytes at BIOSSEG never run.
   Discussed further in section 4.

Other smaller patches are gated on `ESP_PLATFORM` or `_WIN32` and
documented inline (e.g. ESP-IDF newlib has no `<sys/timeb.h>`, so
`struct timeb` and `ftime()` are stubbed at `esp8086.c` lines 31-40).
SDL audio/video and Win32 conio paths from upstream are removed.

The instruction execution loop body (after the BIOSSEG trap and the
max-steps counter) is byte-identical to upstream 8086tiny. We are not
reimplementing the 8086 — we are running 8086tiny with three structural
fixes to make it embed in firmware and survive the way 86-DOS uses
memory.

### `firmware/components/bios/bios.c` — 306 lines

The 86-DOS kernel calls into BIOS via `CALL FAR 0x0040:0xNN`. In a real
PC, that landed in IO.SYS / IBMBIO.COM. Here, the BIOSSEG trap in
`esp8086.c` dispatches to `bios_handle_call(ip)` in this file, with `ip`
selecting one of nine handlers per `bios.h` lines 32-40:

```
0x03  STAT     console status
0x06  IN       console input -> AL
0x09  OUT      AL -> console output
0x0C  PRINT    AL -> printer  (stub: no-op)
0x0F  AUXIN    serial input   (stub: returns Ctrl-Z)
0x12  AUXOUT   AL -> serial   (stub: no-op)
0x15  READ     AL=drv, BX=buf, CX=count, DX=sector — disk read
0x18  WRITE    same args                          — disk write
0x1B  DSKCHG   AL=drv -> AH = no-change
```

Console I/O routes through `usb_serial_jtag_*`, because the
T-Display-S3's USB-C is wired to the ESP32-S3's native USB-Serial-JTAG
peripheral, not to UART0. Output bytes are line-buffered into
`ESP_LOGI` so 86-DOS output does not interleave with ESP-IDF's heartbeat
log on the same channel.

Disk I/O reads/writes the `dos_disk` flash partition (see
`firmware/partitions.csv`: 384 KB raw partition at flash offset
`0x310000`). Each sector is 512 bytes; `bios_read` does
`esp_partition_read` directly into emulated memory at
`(DS << 4) + buf_off`. `bios_write` erases the covering 4 KB flash
sectors then re-writes (NOR flash requires erase-before-write).
Drive 0 only — any other drive returns CF=1 so the kernel's own error
recovery runs.

### `firmware/main/espdos.c` — 163 lines

Boot orchestration. Runs at `app_main`:

1. Print the espDos banner.
2. `bios_init()` — install the USB-Serial-JTAG driver and find the
   `dos_disk` partition.
3. `emu_alloc_mem()` — allocate the 1 MB + 64 KB emulator memory in
   PSRAM.
4. Load the 8086tiny BIOS lookup tables at `0xF000:0x0100`
   (`emu_load_bios_tables()`).
5. Load `bootstub.bin` at `0x0050:0x0000`.
6. Load `kernel.bin` (or `hello.bin` if `ESPDOS_PAYLOAD_HELLO` is
   defined — a confidence harness that exercises every BIOSSEG entry
   with deterministic output) at `0x0100:0x0100`.
7. Set `CS:IP = 0x0050:0x0000` (the bootstub's entry).
8. Run the emulator in 5,000-instruction beats up to 200 beats, logging
   `CS:IP` and `AX` between beats.

There is no other code path. The whole boot is linear and visible in
this file.

## 4. Emulated vs. native: the trust boundary

Every line in this section maps to a specific runtime behavior.

- **8086 instruction stream: emulated.** Every instruction Paterson
  wrote runs on the `esp8086` interpreter, which is C running on the
  ESP32-S3's Xtensa core. There is no x86 silicon involved. We are not
  recompiling the kernel to native ARM/Xtensa — we are emulating the
  8086 the kernel was written for.
- **Console I/O: native, not emulated.** Bytes written by `BIOSOUT`
  cross the BIOSSEG trap and exit emulation as ASCII chars on the USB
  CDC channel. `BIOSIN` reads come from the same channel. There is no
  emulated 16450 UART or PIC.
- **Disk I/O: native, not emulated.** `BIOSREAD` becomes
  `esp_partition_read`. We do not emulate an FDC, an ST-506 controller,
  or a sector translator. The kernel's calling convention (AL=drv,
  BX=buf, CX=count, DX=sector — see `86DOS.ASM` lines 1093-1107) is
  honored at the BIOS-handler level, not at the I/O port level.
- **Boot: native ESP-IDF.** Espressif's standard second-stage bootloader
  loads our firmware. Our firmware loads the 8086tiny BIOS tables, the
  bootstub, and the kernel into emulated memory, then sets `CS:IP` and
  starts emulating. There is no emulated PC ROM BIOS.
- **Timer/RTC: emulated, but unused.** 8086tiny's GET_RTC opcode handler
  is present but 86-DOS 1.00 never triggers it (DOS 1.0 has its own
  date prompt and does not use the PC BIOS RTC). On ESP-IDF we stub
  `struct timeb` / `ftime()` since newlib does not ship them.
- **Two BIOS layers, named confusingly.** This is the most surprising
  thing about the address map. `BIOSSEG = 0x0040` is *Paterson's*
  BIOS — the location 86-DOS does `CALL FAR 0x0040:0xNN` into. Bytes at
  that address are never executed; we trap and dispatch to `bios.c`.
  Separately, `EMU_BIOS_SEG = 0xF000` (= `REGS_BASE >> 4`) is *8086tiny's*
  BIOS-area lookup-table image, which the interpreter itself reads to
  decode opcodes. They serve different purposes and live at different
  addresses; both are called "BIOS" in the source comments.

## 5. What is not in v1

**No COMMAND.COM.** The 86-DOS source tree includes COMMAND.ASM
separately; we do not assemble or load it. Once `DOSINIT` finishes and
the banner prints, there is no shell reading a prompt. The kernel sits
waiting for its `INT 21h` ABI to be called, but nothing on the
emulator side calls it.

The eventual fix, marked Tier 4 in the project plan, is `SHELL.COM` — a
small COMMAND.COM-shaped program that reads a line and dispatches.
**It is not implemented yet.** A reader trying espDos today will see
the banner and a blinking cursor and nothing else interactive. That is
the current state and we should not pretend otherwise.

Other gaps:

- The disk image is empty (`tools/build_disk.py` lines 41-67 build a
  bare FAT12 layout: media byte + EOC markers in both FATs, zeroed
  root directory, zeroed data area). There are no files to load. With
  no shell and no files, the disk is mostly there to make `BIOSREAD`
  not return errors during init.
- `BIOSPRINT` and `BIOSAUXOUT` are no-ops. `BIOSAUXIN` returns Ctrl-Z
  (`0x1A`, end-of-file) so any kernel code that reads from AUX
  terminates cleanly.
- Only one drive is configured (drive 0 / A:). The DPB init table in
  the bootstub declares `NUMDRV = 1`.
- We do not emulate INT 13h hardware-style disk I/O. The kernel never
  uses INT 13h directly — it always goes through `BIOSSEG` — so this
  is not a behavioral gap, but it is a gap if someone later loads
  third-party DOS software that bypasses BIOSSEG.

## 6. Why this is defensible

The pitch is "Tim Paterson's actual 86-DOS source code running on a $5
chip." Walk the chain:

1. **The source is unmodified.** `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM`
   is read-only. Nothing in the build pipeline mutates it. The
   preprocessor reads it on stdin or as an argv file, writes a
   translated copy to `build/kernel.translated.asm`, and stops.
2. **The translation is mechanical.** Every rule in `scp_to_nasm.py`
   is a syntactic rewrite. Most of them (R1, R2, R3, R6, R8, R9, R10,
   R11, R12, R14, R15, R16, R17, R18, R20, R21, R22) are pure dialect
   conversions that produce byte-identical machine code. R4, R5 add
   defaults that SCP-ASM 2.43 supplied implicitly. R7 reorganizes
   layout directives without changing where bytes land. The only rules
   that produce *different* machine code from what SCP-ASM 2.43 would
   produce are R13, R13b, R19, R19b — and they substitute one valid
   8086 instruction sequence for another, preserving control flow
   exactly. We do not edit Paterson's logic.
3. **The kernel binary runs unmodified.** `build/kernel.bin` is loaded
   into emulator memory at the address the assembler intended (`org
   0x100`, `PUT 100H` at 86DOS.ASM line 166). Initial CS:IP after the
   bootstub's far jump is `0x0100:0x0100`. The kernel takes it from
   there.
4. **The 8086 emulator is ours but clearly derived.** The lineage is
   credited at the top of the file (and the LICENSE in `third_party/8086tiny/`
   is preserved). Three structural changes are documented in the same
   comment block. The instruction loop is byte-identical to upstream.
   We are not hiding 8086tiny inside the project.
5. **The BIOS, boot, and disk environment are ours and they match the
   contracts the kernel expects.** Calling conventions, register
   layouts, IVT vector numbers, and DPB field order are all derived
   from specific line numbers in `86DOS.ASM` (cited in `bootstub.asm`,
   `bios.h`, `bios.c`). Where we diverge from a real PC (USB-CDC
   instead of UART, flash partition instead of FDC), we diverge at the
   handler level — the kernel sees the same calling convention either
   way.

Anyone reading the code can verify each link. The artifacts they would
need to inspect:

- `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` — the source.
- `asm/scp_to_nasm.py` — every transformation, with regexes and
  comments.
- `build/kernel.translated.asm` — the NASM-syntax output.
- `build/kernel.bin` — the assembled binary.
- `firmware/components/esp8086/esp8086.c` (and `third_party/8086tiny/8086tiny.c`
  for diffing) — the emulator.
- `tests/emu/` — six host-side tests that compile the same `esp8086.c`
  the firmware uses, exercise it with small bytecode programs, and
  check state via the public API. `test_kernel_banner.c` runs the real
  kernel through bootstub + BIOS-handler stubs and asserts the banner
  text comes out byte-exact.

If any of those steps fails to convince a reader, the failure is
visible at the file level — there is no hidden state.

## QEMU iteration loop, and the KEYBOARD_DRIVER bug it caught

Flashing the T-Display-S3 to chase a bug is a 90-second round trip:
build, erase, flash, reset, watch the monitor, hope. Two days of that
chasing one halt convinced us there had to be a faster loop.

There is. ESP-IDF ships with QEMU, but the version it bundles
(qemu-system-xtensa 9.0, June 2024) cannot emulate the S3's octal
PSRAM controller — the SPIRAM init fails with `octal_psram: PSRAM ID
read error: 0x00000000` and any program that allocates from SPIRAM
faults at boot. Espressif added S3 octal PSRAM support to their
QEMU fork in February 2025; the binary that works is
`esp-develop-9.2.2-20260417` from
[github.com/espressif/qemu/releases](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417)
(asset `qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-x86_64-w64-mingw32.tar.xz`
on Windows). The required flag is `-global driver=ssi_psram,property=is_octal,value=true`.

Full recipe, from a clean firmware tree:

```
# 1. Build firmware as usual.
cd firmware && idf.py build

# 2. Merge bootloader + partition table + app + disk image into one flat
#    image. The disk.img at 0x310000 is the FAT12 volume containing
#    HELLO.COM and MANDEL.COM; if you forget it, the kernel reads
#    flash padding bytes as transient code and execution diverges.
esptool.py --chip esp32s3 merge_bin -o build/qemu_flash.bin \
    --flash_mode dio --flash_size 8MB --fill-flash-size 8MB \
    0x0      build/bootloader/bootloader.bin \
    0x8000   build/partition_table/partition-table.bin \
    0x10000  build/espdos.bin \
    0x310000 ../build/disk.img

# 3. Boot it in QEMU. Serial UART0 is redirected to a file we can tail.
qemu-system-xtensa.exe \
    -nographic -machine esp32s3 -m 8M \
    -global driver=ssi_psram,property=is_octal,value=true \
    -drive file=build/qemu_flash.bin,if=mtd,format=raw \
    -serial file:qemu_serial.log
```

The output is byte-identical to hardware: the kernel boots, banner
prints, date prompt fires, transient loads, MANDEL.COM renders. One
iteration: ~5 seconds vs. ~90 on hardware.

This loop earned its keep on the very first bug it found. The
hardware was halting at `CS:IP=0:0` with `AX=0x094e` after the date
prompt. With QEMU, we could add a 64-entry `pc_interrupt` ring buffer
to `esp8086.c`, dump it on halt, and reproduce the failure
deterministically in seconds. The dump showed `INT 7` firing
unprovoked between every BIOSSEG far-call — and `IVT[7]` was zero, so
each fire pushed flags+CS+IP onto the stack and jumped to `0:0`.

The cause was 8086tiny's `KEYBOARD_DRIVER` macro:

```c
#define KEYBOARD_DRIVER read(0, mem + 0x4A6, 1) && \
    (int8_asap = (mem[0x4A6] == 0x1B), pc_interrupt(7))
```

It runs once per emulated instruction. On a Linux/Windows host with a
real terminal, `read(0,…)` returns `-1` (`EAGAIN`) when stdin is empty
and the macro short-circuits to a no-op. On ESP-IDF stdin is a USB
Serial JTAG endpoint that always has buffered framing data — `read`
returns >0 on every step, the comma-chain runs to completion,
`pc_interrupt(7)` fires, and execution jumps to whatever `IVT[7]`
holds. For us that was `0:0`.

The kernel doesn't need the macro: BIOS keystrokes arrive via a
BIOSSEG `IN` far-call, not `INT 7`. So on `ESP_PLATFORM` we compile
the macro out:

```c
#ifdef ESP_PLATFORM
#define KEYBOARD_DRIVER 0  /* kernel reads keys via BIOSSEG IN; no INT 7 path */
#elif defined(_WIN32)
#define KEYBOARD_DRIVER kbhit() && (mem[0x4A6] = getch(), pc_interrupt(7))
#else
#define KEYBOARD_DRIVER read(0, mem + 0x4A6, 1) && \
    (int8_asap = (mem[0x4A6] == 0x1B), pc_interrupt(7))
#endif
```

Three lines of preprocessor, found and verified inside one afternoon
of QEMU iteration after two prior days of speculative flash cycles.
The integrity argument here is the same as elsewhere: the fix is
local, the reason is documented, and the tooling that found it is
reproducible by anyone with the binary linked above.

## Multiple transients on one disk

The same loader/bootstub pair runs four different transient programs
off the FAT12 disk image:

| Program     | Bytes | Cluster | Loader variant       | Selected by                        |
|-------------|------:|--------:|----------------------|------------------------------------|
| HELLO.COM   |   232 |       2 | bootstub.bin         | `idf.py build -DESPDOS_LOADER_HELLO=1` |
| MANDEL.COM  |   450 |       3 | bootstub_mandel.bin  | (default)                          |
| COUNT.COM   |    71 |       4 | bootstub_count.bin   | `idf.py build -DESPDOS_LOADER_COUNT=1` |
| SHELL.COM   |   227 |       5 | bootstub_shell.bin   | `idf.py build -DESPDOS_LOADER_SHELL=1` |

The first three are non-interactive: they print and `INT 20h`. The
fourth — SHELL.COM — is the first interactive program. Loaded the
same way, it prints a numbered menu, calls `INT 21h AH=07` (RAWINP)
to read one digit, BIOSREADs the chosen sector into a fresh segment
(`CHILD_SEG = 0x3000`), and `JMP FAR` to it. Pick `1`/`2`/`3` and you
get the corresponding program; anything else exits.

Two subtleties worth knowing:

1. **`AH=07`, not `AH=01`.** AH=01 (CONIN) routes through the
   kernel's `INCHK` routine, which the kernel *also* invokes during
   every `CONOUT` as a Ctrl-C/S/P/N "input snoop." Combined with our
   auto-feed BIOSIN, that snoop ate the AUTOPICK digit during the
   menu print, leaving SHELL's later `AH=01` to block forever on
   JTAG. AH=07 (RAWINP) is a direct `CALL BIOSSEG:BIOSIN` — no snoop
   layer, so the auto-feed digit is intact when SHELL reads it.

2. **`-DESPDOS_AUTOPICK=N` must reach two components.** The shell's
   first BIOSIN call after the date prompt comes from the auto-feed
   buffer in `bios.c`, which compiles to `"1-1-80\r" "N" "\r"` only
   if `bios.c` itself sees `ESPDOS_AUTOPICK`. ESP-IDF's
   `target_compile_definitions(${COMPONENT_LIB} ...)` only scopes to
   the component it's invoked in, so the flag is now declared in
   *both* `firmware/main/CMakeLists.txt` (consumed by `espdos.c` for
   the loader-variant `#elif`) and `firmware/components/bios/CMakeLists.txt`
   (consumed by `bios.c` for the auto-feed string). Forgetting one
   side produces a build that loads SHELL.COM but hangs on the first
   prompt with no diagnostic.

QEMU verification with auto-feed:

```
idf.py build -DESPDOS_LOADER_SHELL=1 -DESPDOS_AUTOPICK=2
# ... merge_bin and run as before ...
# Output: shell prints menu, echoes "2", loads COUNT.COM, prints 1..50.
```

## Appendix: file inventory

| Path                                                            | Lines | Bytes  | Origin                       |
|-----------------------------------------------------------------|------:|-------:|------------------------------|
| `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM`         | 3,621 | source | Tim Paterson (1981); read-only |
| `build/kernel.bin`                                              |     - |  6,341 | Built from `86DOS.ASM`       |
| `build/kernel.translated.asm`                                   |     - | 88,625 | Built from `86DOS.ASM`       |
| `build/bootstub.bin`                                            |     - |     74 | Built from `bootstub.asm`    |
| `build/disk.img`                                                |     - |393,216 | Built from `tools/build_disk.py` |
| `asm/scp_to_nasm.py`                                            |   623 |      - | espDos                       |
| `asm/bootstub.asm`                                              |   117 |      - | espDos                       |
| `asm/build_kernel.sh`                                           |    77 |      - | espDos                       |
| `asm/hello.asm`                                                 |     - |      - | espDos (BIOS confidence harness) |
| `firmware/components/esp8086/esp8086.c`                         |   929 |      - | Forked from 8086tiny v1.25   |
| `asm/loader.asm`                                                |    97 |      - | espDos (transient loader)    |
| `asm/mandel.asm`                                                |     - |    450 | espDos (Q4.12 Mandelbrot)    |
| `asm/hellotr.asm`                                               |    28 |    232 | espDos (HELLO.COM transient) |
| `asm/count.asm`                                                 |    52 |     71 | espDos (COUNT.COM 1..50)     |
| `asm/shell.asm`                                                 |   100 |    227 | espDos (SHELL.COM dispatcher)|
| `third_party/8086tiny/8086tiny.c`                               |   774 |      - | Adrian Cable, MIT (upstream) |
| `firmware/components/bios/bios.c`                               |   306 |      - | espDos                       |
| `firmware/components/bios/include/bios.h`                       |    56 |      - | espDos                       |
| `firmware/main/espdos.c`                                        |   163 |      - | espDos                       |
| `tools/build_disk.py`                                           |    79 |      - | espDos                       |
| `tests/emu/Makefile`                                            |    61 |      - | espDos                       |
| `tests/emu/test_*.c` (six tests)                                |     - |      - | espDos                       |

About 2,300 lines of code outside Paterson's source carry the kernel to
the chip. About 6,400 bytes of his binary are what actually runs.
