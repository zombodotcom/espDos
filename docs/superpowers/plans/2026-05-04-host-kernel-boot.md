# Host Kernel Boot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Windows, boot Tim Paterson's `86DOS.ASM` to its date prompt by assembling it with NASM and running the resulting binary on an 8086 emulator with stdio-backed BIOS handlers. Validate the assembler-pipeline + emulator-correctness + BIOS-interface story before any ESP-IDF work.

**Architecture:** `86DOS.ASM` (unmodified) + `kernel.patches.asm` (NASM-syntax-only tweaks if needed) → NASM → `kernel.bin` (flat binary). A small C host driver loads `kernel.bin` into a 96 KB memory buffer at the segment the kernel expects, hands the buffer to a vendored `8086tiny` instruction interpreter, and watches for far calls to `BIOSSEG` (0x0040) — those trap into C BIOS handlers that route console to `stdio` and disk to a RAM-backed FAT12 image.

**Tech Stack:** NASM (kernel assembly), `8086tiny` (Adrian Cable, CC0 — vendored), C99 (MinGW-w64 GCC 15.2), `mingw32-make`. Host-only — no ESP-IDF in this plan.

**Source files referenced (do not modify):**
- `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` — the kernel; treat as read-only

---

## File structure (after this plan)

```
esp-dos/
├── asm/
│   ├── README.md              # how to build the kernel
│   ├── kernel.patches.asm     # NASM-syntax adjustments (if any)
│   └── build_kernel.sh        # invokes NASM, produces kernel.bin
├── host/
│   ├── emu_8086.h
│   ├── emu_8086.c             # adapted 8086tiny core
│   ├── bios.h
│   ├── bios_dispatch.c        # far-call trap → handler routing
│   ├── bios_console.c         # stdio console handlers
│   ├── bios_disk.c            # RAM-disk handlers (host only)
│   ├── disk_image.h
│   ├── disk_image.c           # 320 KB FAT12 buffer + initializer
│   └── main.c                 # entry point: load kernel, run
├── third_party/
│   └── 8086tiny/              # vendored source (CC0)
├── build/                     # gitignored: kernel.bin, .o, .exe
└── Makefile
```

Responsibilities:
- `emu_8086.{h,c}` — instruction interpreter; exposes `void emu_run(emu_t*)` and a callback `void on_far_call(emu_t*, word seg, word off)` invoked when CS:IP changes via CALL FAR
- `bios_dispatch.{h,c}` — knows the 9 BIOSSEG offsets (per `86DOS.ASM:130-143`); reads emu register state; calls the matching handler in console/disk; writes return values + flags back; simulates RETF
- `bios_console.{h,c}` — `bios_stat`, `bios_in`, `bios_out`, `bios_print`, `bios_auxin/out` against `getchar`/`putchar`
- `bios_disk.{h,c}` — `bios_read`, `bios_write`, `bios_dskchg` against a 320 KB RAM buffer
- `disk_image.{h,c}` — owns the 320 KB buffer, initializes it as an empty FAT12 (media 0xFF, geometry 512/2/1/2/112/640)
- `main.c` — orchestrates: read kernel.bin, place at expected segment, set CS:IP, run

---

## Task 1: Project skeleton + Makefile

**Files:**
- Create: `Makefile`, `asm/README.md`, `asm/build_kernel.sh`
- Create: `.gitignore` additions (`build/`, `*.bin`)
- Create: empty placeholder `host/main.c` so Makefile has a target

- [ ] **Step 1: Probe NASM availability**

Run: `which nasm 2>&1 || where nasm 2>&1`

Expected: a path to `nasm.exe`. If not installed, install via `winget install nasm` or `choco install nasm`. Document the version (`nasm -v`).

- [ ] **Step 2: Update `.gitignore`**

Replace existing `.gitignore` with:

```
# Build artifacts
build/
*.exe
*.o
*.a
*.obj
*.bin

# IDE / editor
.vscode/
.idea/
*.swp
*~

# ESP-IDF (future)
sdkconfig.old
managed_components/
dependencies.lock
```

- [ ] **Step 3: Write `asm/README.md`**

```markdown
# Kernel build

Sources:
- `../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` — Tim Paterson's
  86-DOS 1.00 kernel; treated as read-only
- `kernel.patches.asm` — NASM-syntax adjustments only; no logic changes

Build:
```
./build_kernel.sh
```

Output: `../build/kernel.bin` (flat binary, ~10–14 KB).
```

- [ ] **Step 4: Write `asm/build_kernel.sh`**

```sh
#!/bin/sh
set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"
OUT_DIR="$ROOT/build"
mkdir -p "$OUT_DIR"

# We do NOT modify 86DOS.ASM in place. If NASM-specific adjustments are
# needed, kernel.patches.asm wraps the original via %include + macro
# overrides, and we assemble that file instead.
INPUT="$SCRIPT_DIR/kernel.patches.asm"
[ -f "$INPUT" ] || INPUT="$SRC"

nasm -f bin -o "$OUT_DIR/kernel.bin" -l "$OUT_DIR/kernel.lst" "$INPUT"
echo "Built: $OUT_DIR/kernel.bin ($(wc -c < "$OUT_DIR/kernel.bin") bytes)"
```

Mark executable: `chmod +x asm/build_kernel.sh`.

- [ ] **Step 5: Write the Makefile**

```makefile
HOST_DIR     := host
BUILD_DIR    := build
THIRDPARTY   := third_party

CFLAGS := -std=c99 -Wall -Wextra -I$(HOST_DIR) -I$(THIRDPARTY)/8086tiny -g

# Listed individually so the Makefile reflects the public surface explicitly.
HOST_SRCS := \
    $(HOST_DIR)/emu_8086.c \
    $(HOST_DIR)/bios_dispatch.c \
    $(HOST_DIR)/bios_console.c \
    $(HOST_DIR)/bios_disk.c \
    $(HOST_DIR)/disk_image.c \
    $(HOST_DIR)/main.c

EMU_SRCS := $(THIRDPARTY)/8086tiny/8086tiny.c

all: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/dos_host.exe

$(BUILD_DIR)/kernel.bin:
	asm/build_kernel.sh

$(BUILD_DIR)/dos_host.exe: $(HOST_SRCS) $(EMU_SRCS) | $(BUILD_DIR)
	gcc $(CFLAGS) $^ -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
```

- [ ] **Step 6: Write a placeholder `host/main.c`**

```c
#include <stdio.h>
int main(void) {
    fprintf(stderr, "[host] dos_host placeholder; build pipeline OK.\n");
    return 0;
}
```

(Other host/*.c will arrive in later tasks. Until then, the Makefile won't be able to link `dos_host.exe` since it references non-existent files. That's fine — Task 1's success criterion is just that `kernel.bin` builds and the Makefile is syntactically correct.)

- [ ] **Step 7: Verify NASM can find the source path**

Run: `ls "../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"`

Expected: file exists. If not, the `Paterson-Listings` repo is missing (cloned earlier in this project at `C:\Users\zombo\desktop\programming\dosNew\Paterson-Listings`). Re-clone with: `git clone https://github.com/DOS-History/Paterson-Listings.git ../Paterson-Listings`.

- [ ] **Step 8: Commit**

```bash
git add .gitignore Makefile asm/README.md asm/build_kernel.sh host/main.c
git commit -m "scaffold: project tree + Makefile for emulated-kernel host build"
```

---

## Task 2: Assemble `86DOS.ASM` with NASM

NASM may or may not accept the original source as-is. We try first, then iterate on the smallest possible patch file — never editing `86DOS.ASM` in place.

**Files:**
- Possibly create: `asm/kernel.patches.asm` (if the original needs wrappers)

- [ ] **Step 1: First attempt — assemble the original directly**

Run from repo root:
```
asm/build_kernel.sh 2>&1 | tee build/nasm-attempt-1.log
```

Two possible outcomes:

A. **It works.** `build/kernel.bin` exists, non-empty. Skip to Step 5.

B. **NASM emits errors.** The script reads errors from the log. Common SCP-ASM-vs-NASM differences:
   - SCP ASM uses `DS 3` for "reserve 3 bytes" (Define Storage); NASM uses `RESB 3`
   - SCP ASM may use `EQU` differently (NASM is similar but stricter)
   - Segment notation: `SEG:OFF` vs `SEG OFFSET OFF`
   - `CALL label,SEG` (SCP-ASM far call) vs `CALL FAR SEG:OFFSET` (NASM)
   - `ORG` directives: NASM `org` is similar but SCP may differ in interaction with segments

- [ ] **Step 2: If errors — read them, classify, write `kernel.patches.asm`**

The `kernel.patches.asm` strategy: do NOT copy the kernel and edit. Instead, let NASM include it AFTER we've defined NASM-friendly macros that translate the differing syntax:

```asm
; kernel.patches.asm — NASM-syntax wrapper for 86DOS.ASM.
; PATCH (NASM): reason
; This file MUST NOT contain kernel logic. Only assembler-syntax tweaks.

%define DS RESB           ; SCP "DS n" → NASM "RESB n"
; (additional %defines / %macros as discovered)

%include "../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"
```

Add `%define`s / `%macro`s ONE AT A TIME, re-running NASM after each. Each addition gets a `; PATCH (NASM): <one-line why>` comment.

If a directive fundamentally can't be macro'd (e.g., NASM rejects a structural construct), STOP and escalate — modifying the kernel in place would defeat the project's "build from source" pitch.

- [ ] **Step 3: Iterate until clean assembly**

Re-run `asm/build_kernel.sh` after each change. Success looks like:

```
Built: build/kernel.bin (NNNN bytes)
```

with no NASM warnings. (Some NASM warnings — like "section 'XYZ' is empty" — may be benign.)

- [ ] **Step 4: Sanity-check the output**

Run: `od -An -t x1 -N 32 build/kernel.bin`

Expected: looks like 8086 instruction bytes, not all zeros, not text. Sample what should be at offset 0 — depends on what `86DOS.ASM` `ORG`s to. Note this for later (the host loader will need to know).

Check the listing file for the entry-point offset:
```
grep -n "ENTRY\|DOSINIT\|^==" build/kernel.lst | head
```

Record:
- Kernel size: ____ bytes
- Entry point label and address: ________

- [ ] **Step 5: Commit**

```bash
git add asm/kernel.patches.asm  # only if it exists
git commit -m "asm: assemble 86DOS.ASM with NASM (patches: <count> syntax tweaks)"
```

If no patches were needed (Outcome A), there's nothing to commit. The build script alone (committed in Task 1) is the artifact.

---

## Task 3: Vendor `8086tiny` and run a synthetic test program

We adopt Adrian Cable's `8086tiny` as the emulator core. Goal here: get the unmodified `8086tiny` building inside our project, executing a tiny test program of our own, before we point it at the real kernel.

**Files:**
- Create: `third_party/8086tiny/8086tiny.c`, `third_party/8086tiny/LICENSE`
- Create: `host/emu_8086.h`
- Modify: `host/main.c` (use the emulator)
- Create: `asm/test_program.asm` (8086 hello-world)

- [ ] **Step 1: Vendor 8086tiny**

Adrian Cable's repo is at `https://github.com/adriancable/8086tiny`. We need the readable variant (`8086tiny.c`) plus its license:

```
mkdir -p third_party/8086tiny
git clone --depth 1 https://github.com/adriancable/8086tiny /tmp/8086tiny
cp /tmp/8086tiny/8086tiny.c third_party/8086tiny/
cp /tmp/8086tiny/LICENSE third_party/8086tiny/  # CC0 / public domain
rm -rf /tmp/8086tiny
```

- [ ] **Step 2: Read the vendored source briefly**

Skim `8086tiny.c` (~700 lines). Identify:
- Global memory buffer (often `mem[]` with `RAM_SIZE`)
- Where `INT` opcodes are handled (look for `case 0xCD:` or similar)
- Whether it has an `extern` hook for far CALLs, or whether we'll need to add one

Record findings as comments at the top of `host/emu_8086.h`.

- [ ] **Step 3: Build standalone first — verify vendored 8086tiny compiles**

Add a temporary main file `third_party/8086tiny/standalone_main.c`:

```c
/* TEMPORARY — verifies 8086tiny.c builds cleanly in our environment.
 * Removed in Step 5. */
#include <stdio.h>
int main(void) { fprintf(stderr, "8086tiny standalone link OK\n"); return 0; }
```

Run: `gcc -c third_party/8086tiny/8086tiny.c -o build/8086tiny.o`

Expected: compiles (warnings allowed, errors not). If errors, document them — `8086tiny` has been rebuilt many times for many compilers; modern GCC may warn about implicit-int or K&R-style declarations. Minor edits to `8086tiny.c` *are* allowed since we vendored it; tag them with `/* PATCH (GCC15): reason */`.

Delete `third_party/8086tiny/standalone_main.c` once `8086tiny.o` builds.

- [ ] **Step 4: Write a tiny synthetic 8086 test program**

`asm/test_program.asm`:
```asm
; test_program.asm — minimal 8086 program to verify our emulator pipeline.
; Outputs ASCII '!' to a hooked port and halts.

bits 16
org 0

start:
    mov al, '!'
    out 0xE0, al    ; we'll hook port 0xE0 in the host as "putchar"
    hlt
```

Build: `nasm -f bin -o build/test_program.bin asm/test_program.asm`. Result: ~5 bytes.

- [ ] **Step 5: Write `host/emu_8086.h` — minimal API for our use**

```c
#ifndef EMU_8086_H
#define EMU_8086_H

#include <stdint.h>
#include <stddef.h>

/* Adapter around 8086tiny. The host buffer is the emulator's memory space;
 * load programs into it directly before running. */

#define EMU_MEM_SIZE  (96u * 1024u)   /* 96 KB for kernel + user segment */

typedef struct emu emu_t;

/* Construct/destroy. Memory is owned by the emu. */
emu_t *emu_create(void);
void   emu_destroy(emu_t *e);

/* Direct memory access. */
uint8_t *emu_memory(emu_t *e);   /* pointer to base of EMU_MEM_SIZE buffer */

/* Set CS:IP entry point. */
void emu_set_cs_ip(emu_t *e, uint16_t cs, uint16_t ip);

/* Run until HLT or until on_far_call returns "halt". */
void emu_run(emu_t *e);

/* Hooks the host wires up before emu_run. */
typedef void (*emu_far_call_fn)(emu_t *e, uint16_t seg, uint16_t off);
typedef void (*emu_port_out_fn)(emu_t *e, uint16_t port, uint8_t value);

void emu_set_far_call_hook(emu_t *e, emu_far_call_fn cb);
void emu_set_port_out_hook(emu_t *e, emu_port_out_fn cb);

/* Register access for handlers. */
uint16_t emu_get_ax(emu_t *e);   void emu_set_ax(emu_t *e, uint16_t);
uint16_t emu_get_bx(emu_t *e);   void emu_set_bx(emu_t *e, uint16_t);
uint16_t emu_get_cx(emu_t *e);   void emu_set_cx(emu_t *e, uint16_t);
uint16_t emu_get_dx(emu_t *e);   void emu_set_dx(emu_t *e, uint16_t);
uint16_t emu_get_ds(emu_t *e);
uint16_t emu_get_es(emu_t *e);
void     emu_set_carry(emu_t *e, int cf);

#endif
```

(Implementation in `host/emu_8086.c` arrives in Task 4 alongside the trap mechanism. Keeping this header alone for the moment.)

- [ ] **Step 6: Build (will not link yet — that's expected)**

Run: `mingw32-make 2>&1 | tail -10`

Expected: compile errors about undefined `emu_create` / `emu_run` / etc. — Task 4 fills these in. The `kernel.bin` target should succeed.

- [ ] **Step 7: Commit**

```bash
git add third_party/8086tiny host/emu_8086.h asm/test_program.asm
# Note: third_party/8086tiny/LICENSE goes with vendored sources
git commit -m "vendor 8086tiny + minimal emulator header + 8086 hello-world"
```

---

## Task 4: Implement emulator adapter + far-call trap

This is the heart of the project. We wrap `8086tiny`'s instruction loop, expose a `far_call` callback when CS:IP changes via CALL FAR with target segment matching a registered "trap segment," and provide register-access primitives.

**Files:**
- Create: `host/emu_8086.c`
- Modify: `host/main.c` (instantiate emu, run test_program, observe '!' on stdout)

Design choice: rather than detecting CALL FAR opcodes (0x9A or REG/MEM 0xFF /3) in the instruction decoder, **place a sentinel opcode (`HLT`, 0xF4) at every BIOSSEG entry point in emu memory**, then in the HLT handler check whether CS == BIOSSEG; if yes, dispatch the BIOS handler based on IP, simulate RETF, continue. This requires no surgery on the 8086tiny decoder.

- [ ] **Step 1: Inventory `8086tiny`'s API surface**

Open `third_party/8086tiny/8086tiny.c` and write your findings as a comment block at the top of the new `host/emu_8086.c` (created in Step 2). Record exactly:

1. **Memory buffer:** symbol name, type, size. Most variants use a global `uint8_t *mem` of size `RAM_SIZE` or similar.
2. **Register storage:** symbol(s) holding AX/BX/CX/DX/SP/BP/SI/DI/CS/DS/ES/SS/IP/FLAGS. Common variants store AX/CX/DX/BX/SP/BP/SI/DI in a uint16_t array of 8 entries indexed by encoding bits, and segments separately.
3. **Flags storage:** how CF, ZF, etc. are kept (packed in FLAGS, individual bytes, or a struct).
4. **Step granularity:** does the source expose a `step_one_instruction()` function, or is the loop inline in `main()`? If inline, plan to move the loop body into a function.
5. **Existing INT/IRET handling:** where INT instructions are dispatched, since our HLT-as-trap mechanism reuses the same general flow.

Write findings as ~30 lines of comment at the top of `emu_8086.c`. The remaining steps reference these findings.

- [ ] **Step 2: Implement `host/emu_8086.c`**

The shape of the file:

```c
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emu_8086.h"

/* === Findings from 8086tiny inventory (Step 1) ===
 * Memory buffer:    <symbol>, <type>, <size>
 * Registers:        <symbol layout>
 * Flags:            <symbol layout>
 * Step function:    <name or "extracted from main"
 * INT dispatch at:  <line/symbol>
 */

/* If 8086tiny has its own main(), neutralize it: */
#define main tiny_unused_main
#include "8086tiny.c"           /* vendored at third_party/8086tiny/ */
#undef main

struct emu {
    /* Our 96 KB host buffer; we'll repoint 8086tiny's mem pointer here
     * (or copy in/out at run boundaries, depending on whether 8086tiny's
     * memory is dynamically pointed or a fixed array). */
    uint8_t          mem[EMU_MEM_SIZE];
    emu_far_call_fn  far_call;
    emu_port_out_fn  port_out;
    int              halted;
};

emu_t *emu_create(void) {
    emu_t *e = calloc(1, sizeof(*e));
    /* If 8086tiny's `mem` is a pointer: aim it at e->mem.
     * If it's a fixed array smaller than EMU_MEM_SIZE: shrink EMU_MEM_SIZE
     * or adapt 8086tiny to use our buffer (one-line redefine). */
    return e;
}

void emu_destroy(emu_t *e) { free(e); }
uint8_t *emu_memory(emu_t *e) { return e->mem; }

void emu_set_cs_ip(emu_t *e, uint16_t cs, uint16_t ip) {
    /* Write to 8086tiny's CS and IP storage (whatever symbols Step 1
     * found). The (void)e is because the registers are typically global
     * in 8086tiny. */
    (void)e;
    /* CS_register = cs; IP_register = ip; */
}

void emu_set_far_call_hook(emu_t *e, emu_far_call_fn cb) { e->far_call = cb; }
void emu_set_port_out_hook(emu_t *e, emu_port_out_fn cb) { e->port_out = cb; }

void emu_run(emu_t *e) {
    while (!e->halted) {
        /* Capture pre-step CS:IP; needed for HLT-trap below. */
        uint16_t pre_cs = /* read 8086tiny CS */ 0;
        uint16_t pre_ip = /* read 8086tiny IP */ 0;
        uint8_t  opc    = e->mem[((uint32_t)pre_cs << 4) + pre_ip];

        /* Step one instruction by calling the function you found/created
         * in Step 1. */
        /* tiny_step_once(); */

        /* HLT-as-trap. */
        if (opc == 0xF4) {
            if (pre_cs == 0x0040) {
                /* This is a BIOSSEG far-call landing pad. Dispatch. */
                if (e->far_call) e->far_call(e, pre_cs, pre_ip);
                /* Simulate RETF: pop IP, then CS, from emulated stack. */
                uint16_t sp = /* read 8086tiny SP */ 0;
                uint16_t ss = /* read 8086tiny SS */ 0;
                uint8_t *stk = &e->mem[((uint32_t)ss << 4) + sp];
                uint16_t new_ip = (uint16_t)(stk[0] | (stk[1] << 8));
                uint16_t new_cs = (uint16_t)(stk[2] | (stk[3] << 8));
                /* SP += 4; CS = new_cs; IP = new_ip; */
                /* (Actual register-write code goes here using the symbols
                 * recorded in Step 1's inventory.) */
                continue;
            }
            /* User-program HLT — halt the emu. */
            e->halted = 1;
            break;
        }
    }
}

/* Register accessors. Each is one line that reads from 8086tiny's storage. */
uint16_t emu_get_ax(emu_t *e) { (void)e; return /* AX_register */ 0; }
void     emu_set_ax(emu_t *e, uint16_t v) { (void)e; (void)v; /* AX_register = v; */ }
/* Implement bx, cx, dx, ds, es similarly. */

void emu_set_carry(emu_t *e, int cf) {
    (void)e; (void)cf;
    /* Set the CF bit/byte in 8086tiny's flag storage. */
}
```

Replace each `/* commented placeholder */` with concrete code based on the symbols recorded in Step 1. The plan does not show literal symbol names because they depend on which `8086tiny` variant we vendored — but Step 1 produces those names as a comment in this very file, so the implementer is reading from that comment as they fill in each line.

Build + link will fail until every comment is resolved to real code; that's the gate.

- [ ] **Step 2: Wire HLT-as-trap mechanism**

Inside the run loop, after each instruction completes, check if the LAST executed opcode was 0xF4 (HLT). If so:
- Read CS from emu state. If CS == 0x0040 (BIOSSEG, the segment 86DOS expects for the BIOS jump table) AND the previous IP corresponds to one of the 9 BIOS entry offsets (0, 3, 6, 9, 12, 15, 18, 21, 24 per `86DOS.ASM:130-143`), call `e->far_call(e, CS, IP_of_HLT)`.
- After far_call returns, simulate RETF: pop two words from the emulated stack, set IP and CS from those.
- Continue execution. **Do not** mark e->halted = 1 in this case.

For any other HLT (e.g., user code), set `e->halted = 1` and break the loop.

- [ ] **Step 3: Wire OUT-port hook for the test program**

In the OUT-imm8-AL opcode (0xE6), call `e->port_out(e, port, AL)`. The Task 3 test program will use port 0xE0 to print '!'.

- [ ] **Step 4: Update `host/main.c` to run test_program**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_8086.h"

static void on_port_out(emu_t *e, uint16_t port, uint8_t value) {
    (void)e;
    if (port == 0xE0) {
        putchar(value);
        fflush(stdout);
    } else {
        fprintf(stderr, "[host] unexpected OUT port=0x%04x val=0x%02x\n",
                port, value);
    }
}

static long load_file(const char *path, uint8_t *dst, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    long n = (long)fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "build/test_program.bin";
    emu_t *e = emu_create();
    if (!e) { fprintf(stderr, "emu_create failed\n"); return 1; }

    uint8_t *mem = emu_memory(e);
    long n = load_file(path, mem + 0x100, EMU_MEM_SIZE - 0x100);
    if (n <= 0) { emu_destroy(e); return 2; }
    fprintf(stderr, "[host] loaded %ld bytes from %s at offset 0x100\n", n, path);

    emu_set_port_out_hook(e, on_port_out);
    emu_set_cs_ip(e, 0x0000, 0x0100);  /* like a .COM file */
    emu_run(e);

    emu_destroy(e);
    fprintf(stderr, "\n[host] emulator halted normally\n");
    return 0;
}
```

- [ ] **Step 5: Build and run the synthetic test**

```
mingw32-make build/test_program.bin
mingw32-make build/dos_host.exe
./build/dos_host.exe build/test_program.bin
```

Expected output:
```
[host] loaded 5 bytes from build/test_program.bin at offset 0x100
!
[host] emulator halted normally
```

If `!` doesn't appear: the OUT hook isn't wired or the emulator's instruction step is broken. Diagnose with single-step prints.

- [ ] **Step 6: Commit**

```bash
git add host/emu_8086.c host/main.c
git commit -m "emu: 8086tiny adapter, HLT-as-trap mechanism, port-out hook (synthetic test passes)"
```

---

## Task 5: BIOS dispatch + console handlers

Now wire the BIOSSEG far-call trap. Place HLT bytes at the 9 BIOS entry offsets in emu memory before kernel run; on HLT-at-BIOSSEG, dispatch.

**Files:**
- Create: `host/bios.h`, `host/bios_dispatch.c`, `host/bios_console.c`
- Modify: `host/main.c`

- [ ] **Step 1: Write `host/bios.h`**

```c
#ifndef BIOS_H
#define BIOS_H

#include "emu_8086.h"

/* BIOS jump-table layout per 86DOS.ASM:130-143. Each entry is a 3-byte
 * far-jump stub in the original; in our emulator each is one HLT byte
 * and our HLT-trap recognizes CS=BIOSSEG + offset = one of these. */
#define BIOSSEG          0x0040
#define BIOS_OFF_STAT    0x03   /* BIOSSTAT  — console status   */
#define BIOS_OFF_IN      0x06   /* BIOSIN    — console input    */
#define BIOS_OFF_OUT     0x09   /* BIOSOUT   — console output   */
#define BIOS_OFF_PRINT   0x0C   /* BIOSPRINT — printer output   */
#define BIOS_OFF_AUXIN   0x0F   /* BIOSAUXIN                    */
#define BIOS_OFF_AUXOUT  0x12   /* BIOSAUXOUT                   */
#define BIOS_OFF_READ    0x15   /* BIOSREAD  — disk read        */
#define BIOS_OFF_WRITE   0x18   /* BIOSWRITE — disk write       */
#define BIOS_OFF_DSKCHG  0x1B   /* BIOSDSKCHG                   */

void bios_install_traps(emu_t *e);
void bios_dispatch(emu_t *e, uint16_t seg, uint16_t off);

/* Console (bios_console.c) */
uint8_t bios_console_stat(emu_t *e);
uint8_t bios_console_in(emu_t *e);
void    bios_console_out(emu_t *e, uint8_t ch);
void    bios_console_print(emu_t *e, uint8_t ch);
uint8_t bios_console_auxin(emu_t *e);
void    bios_console_auxout(emu_t *e, uint8_t ch);

/* Disk (bios_disk.c) — implemented in Task 6 */
int     bios_disk_read(emu_t *e, uint8_t drive, uint16_t buf_seg,
                       uint16_t buf_off, uint16_t count, uint16_t sector);
int     bios_disk_write(emu_t *e, uint8_t drive, uint16_t buf_seg,
                        uint16_t buf_off, uint16_t count, uint16_t sector);
int     bios_disk_dskchg(emu_t *e, uint8_t drive);

#endif
```

- [ ] **Step 2: Write `host/bios_dispatch.c`**

```c
#include <stdio.h>
#include "bios.h"

void bios_install_traps(emu_t *e) {
    uint8_t *mem = emu_memory(e);
    /* Place HLT (0xF4) at each BIOSSEG:offset entry. The kernel's
     * far-call lands here; our HLT trap intercepts and dispatches. */
    uint32_t base = (uint32_t)BIOSSEG << 4;
    static const uint16_t offs[] = {
        BIOS_OFF_STAT, BIOS_OFF_IN, BIOS_OFF_OUT, BIOS_OFF_PRINT,
        BIOS_OFF_AUXIN, BIOS_OFF_AUXOUT, BIOS_OFF_READ, BIOS_OFF_WRITE,
        BIOS_OFF_DSKCHG,
    };
    for (size_t i = 0; i < sizeof(offs)/sizeof(offs[0]); i++) {
        mem[base + offs[i]] = 0xF4;       /* HLT */
        /* Bytes after the HLT are unused; original ASM had a 3-byte
         * far-jump stub.  We trap on the HLT and synthesize RETF. */
    }
}

void bios_dispatch(emu_t *e, uint16_t seg, uint16_t off) {
    if (seg != BIOSSEG) {
        fprintf(stderr, "[bios] far-call to non-BIOSSEG: %04x:%04x\n", seg, off);
        return;
    }
    /* AL=arg, return value in AL; CF for disk error per 86DOS.ASM convention. */
    uint8_t al = (uint8_t)(emu_get_ax(e) & 0xFF);
    switch (off) {
    case BIOS_OFF_STAT:
        emu_set_ax(e, (emu_get_ax(e) & 0xFF00) | bios_console_stat(e));
        break;
    case BIOS_OFF_IN:
        emu_set_ax(e, (emu_get_ax(e) & 0xFF00) | bios_console_in(e));
        break;
    case BIOS_OFF_OUT:    bios_console_out(e, al);    break;
    case BIOS_OFF_PRINT:  bios_console_print(e, al);  break;
    case BIOS_OFF_AUXIN:
        emu_set_ax(e, (emu_get_ax(e) & 0xFF00) | bios_console_auxin(e));
        break;
    case BIOS_OFF_AUXOUT: bios_console_auxout(e, al); break;
    case BIOS_OFF_READ: {
        int err = bios_disk_read(e, al, emu_get_ds(e), emu_get_bx(e),
                                 emu_get_cx(e), emu_get_dx(e));
        emu_set_carry(e, err);
        break;
    }
    case BIOS_OFF_WRITE: {
        int err = bios_disk_write(e, al, emu_get_ds(e), emu_get_bx(e),
                                  emu_get_cx(e), emu_get_dx(e));
        emu_set_carry(e, err);
        break;
    }
    case BIOS_OFF_DSKCHG: {
        int v = bios_disk_dskchg(e, al);
        /* AH<0 changed/unknown, AH=1 not changed (per bios.h doc) */
        emu_set_ax(e, (uint16_t)((v < 0 ? 0xFF : 0x01) << 8));
        break;
    }
    default:
        fprintf(stderr, "[bios] unknown BIOSSEG offset 0x%02x\n", off);
        break;
    }
}
```

- [ ] **Step 3: Write `host/bios_console.c`**

```c
#include <stdio.h>
#include "bios.h"

uint8_t bios_console_stat(emu_t *e) {
    (void)e;
    return 0;   /* never report a key ready; bios_in blocks */
}

uint8_t bios_console_in(emu_t *e) {
    (void)e;
    int c = getchar();
    if (c == EOF) return 0x1A;
    if (c == '\n') return '\r';
    return (uint8_t)c;
}

void bios_console_out(emu_t *e, uint8_t ch) {
    (void)e;
    if (ch == '\r') return;
    putchar(ch);
    fflush(stdout);
}

void bios_console_print(emu_t *e, uint8_t ch) { (void)e; (void)ch; }
uint8_t bios_console_auxin(emu_t *e)            { (void)e; return 0; }
void    bios_console_auxout(emu_t *e, uint8_t c){ (void)e; (void)c; }
```

- [ ] **Step 4: Wire the trap in main.c**

Modify `host/main.c` to register `bios_dispatch` as the far-call handler and call `bios_install_traps` after the emu is created. (The synthetic test program path can stay; we're about to replace the input file with kernel.bin in Task 7.)

```c
emu_set_far_call_hook(e, bios_dispatch);
bios_install_traps(e);
```

- [ ] **Step 5: Build**

```
mingw32-make build/dos_host.exe
```

Expected: builds. Tests still synthetic (kernel-loading comes in Task 7).

- [ ] **Step 6: Commit**

```bash
git add host/bios.h host/bios_dispatch.c host/bios_console.c host/main.c
git commit -m "bios: HLT-trap dispatch + stdio console handlers"
```

---

## Task 6: Disk image + disk BIOS handlers

**Files:**
- Create: `host/disk_image.h`, `host/disk_image.c`, `host/bios_disk.c`

- [ ] **Step 1: Write `host/disk_image.h`**

```c
#ifndef DISK_IMAGE_H
#define DISK_IMAGE_H

#include <stdint.h>

#define DISK_SECSIZ   512u
#define DISK_DSKSIZ   640u
#define DISK_BYTES    (DISK_SECSIZ * DISK_DSKSIZ)   /* 320 KB */

/* The host's RAM-backed disk image, exported so tests + the kernel both
 * see the same bytes. */
extern uint8_t disk_image[DISK_BYTES];

/* Initialize as an empty 320 KB FAT12 DOS-1.0 floppy:
 *  - boot sector zeroed
 *  - FAT 1 (sector 1) starts with media descriptor 0xFF + 0xFF 0xFF
 *  - FAT 2 (sector 3) identical
 *  - rest zero
 */
void disk_image_init_empty_320kb(void);

#endif
```

- [ ] **Step 2: Write `host/disk_image.c`**

```c
#include <string.h>
#include "disk_image.h"

uint8_t disk_image[DISK_BYTES];

void disk_image_init_empty_320kb(void) {
    memset(disk_image, 0, DISK_BYTES);
    static const uint8_t fat_head[3] = { 0xFF, 0xFF, 0xFF };
    memcpy(disk_image + 1 * DISK_SECSIZ, fat_head, 3);   /* FAT 1 */
    memcpy(disk_image + 3 * DISK_SECSIZ, fat_head, 3);   /* FAT 2 */
}
```

- [ ] **Step 3: Write `host/bios_disk.c`**

```c
#include <stdio.h>
#include <string.h>
#include "bios.h"
#include "disk_image.h"

/* Resolve an emulator seg:off into a host pointer into emu memory. */
static uint8_t *emu_ptr(emu_t *e, uint16_t seg, uint16_t off) {
    return emu_memory(e) + ((uint32_t)seg << 4) + off;
}

int bios_disk_read(emu_t *e, uint8_t drive, uint16_t buf_seg,
                   uint16_t buf_off, uint16_t count, uint16_t sector) {
    (void)drive;
    if ((uint32_t)(sector + count) * DISK_SECSIZ > DISK_BYTES) return 1;
    memcpy(emu_ptr(e, buf_seg, buf_off),
           disk_image + (uint32_t)sector * DISK_SECSIZ,
           (size_t)count * DISK_SECSIZ);
    return 0;
}

int bios_disk_write(emu_t *e, uint8_t drive, uint16_t buf_seg,
                    uint16_t buf_off, uint16_t count, uint16_t sector) {
    (void)drive;
    if ((uint32_t)(sector + count) * DISK_SECSIZ > DISK_BYTES) return 1;
    memcpy(disk_image + (uint32_t)sector * DISK_SECSIZ,
           emu_ptr(e, buf_seg, buf_off),
           (size_t)count * DISK_SECSIZ);
    return 0;
}

int bios_disk_dskchg(emu_t *e, uint8_t drive) {
    (void)e; (void)drive;
    return 1;   /* not changed */
}
```

- [ ] **Step 4: Build**

```
mingw32-make build/dos_host.exe
```

Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add host/disk_image.h host/disk_image.c host/bios_disk.c
git commit -m "bios: RAM-backed FAT12 320KB disk image + read/write/dskchg"
```

---

## Task 7: Load and run the kernel

Replace the synthetic-test path in `main.c` with kernel-loading. The kernel needs to be placed at the segment its `ORG` directive expects (recorded in Task 2 Step 4).

**Files:**
- Modify: `host/main.c`

- [ ] **Step 1: Determine kernel load segment from listing**

Check `build/kernel.lst` (produced by NASM in Task 2):
- The `ORG` value sets the offset where the first byte lands
- The expected segment is set by *the program that loads the kernel* — for the original IBM PC bootloader, that was probably `0x0050:0x0000` or `0x0060:0x0000`. Our emulator can place it wherever convenient.

Read `86DOS.ASM`'s comments around line 130-150 — the kernel expects BIOSSEG at 0x0040 (we already arranged that). The kernel's own segment is whatever it's loaded at; it `ORG 0`s and uses CS-relative addressing. So we can load it at any segment that doesn't collide with BIOSSEG (0x0040-0x0040+stub_size) and provides enough room above for the kernel's data area.

Recommendation: load kernel at segment 0x0050 (offset 0x500 in flat memory). The kernel is ~10–14 KB; that leaves room above for FAT/dir buffers + DPBs the kernel allocates dynamically.

Record decision in `host/main.c` as a `#define KERNEL_SEG 0x0050`.

- [ ] **Step 2: Rewrite `host/main.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_8086.h"
#include "bios.h"
#include "disk_image.h"

#define KERNEL_SEG 0x0050

static long load_file(const char *path, uint8_t *dst, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    long n = (long)fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv) {
    const char *kpath = (argc > 1) ? argv[1] : "build/kernel.bin";

    emu_t *e = emu_create();
    if (!e) { fprintf(stderr, "emu_create failed\n"); return 1; }

    /* Empty 320 KB FAT12 image; later tasks will optionally pre-stage files. */
    disk_image_init_empty_320kb();

    /* Install BIOS-segment trap stubs and dispatch. */
    bios_install_traps(e);
    emu_set_far_call_hook(e, bios_dispatch);

    /* Load kernel into emu memory at KERNEL_SEG:0000. */
    uint8_t *kern_base = emu_memory(e) + ((uint32_t)KERNEL_SEG << 4);
    size_t   kern_cap  = EMU_MEM_SIZE - ((uint32_t)KERNEL_SEG << 4);
    long     n         = load_file(kpath, kern_base, kern_cap);
    if (n <= 0) { emu_destroy(e); return 2; }
    fprintf(stderr, "[host] loaded %ld bytes of kernel at %04x:0000\n",
            n, KERNEL_SEG);

    /* Set CS:IP to kernel entry. The original kernel does NOT use 0x100
     * — it ORGs to 0 and starts at the very first byte. Confirm via
     * build/kernel.lst (look for the first instruction). */
    emu_set_cs_ip(e, KERNEL_SEG, 0x0000);

    emu_run(e);

    emu_destroy(e);
    fprintf(stderr, "\n[host] emulator halted\n");
    return 0;
}
```

- [ ] **Step 3: Build**

```
mingw32-make
```

- [ ] **Step 4: First run — observe output**

```
echo "1-1-81" | ./build/dos_host.exe
```

Expected (best case): kernel banner + date prompt + "MS-DOS  version 1.00" or similar. Reality: probably crashes or hangs partway.

Capture exact output. Common early failure modes:
- **Crashes immediately:** kernel entry isn't where we placed CS:IP. Check `build/kernel.lst` again; first executable byte may not be at `ORG 0`. Adjust IP.
- **Garbled output:** BIOSOUT trap might be returning to wrong address (RETF stack synthesis broken). Check Task 4's RETF simulation.
- **Hangs after partial output:** kernel called BIOSIN; we're blocked on stdin. Pipe input as shown.
- **Hangs without output:** kernel jumped into BIOSSEG region but offset doesn't match a known stub. Add diagnostic `fprintf` in `bios_dispatch` for unknown offsets.
- **`Bad FAT` or similar:** disk image isn't FAT12-formatted properly; verify Task 6's init bytes are at the right sectors.

- [ ] **Step 5: Commit (whatever state — failing OK)**

The commit message should describe the current observed state in one phrase, e.g., `host: load kernel.bin into emu (crashes at IP=0xNNNN)` or `host: load kernel.bin into emu (prints partial banner)`. Pick whatever's accurate from Step 4.

```bash
git add host/main.c
git commit
```

---

## Task 8: Iterate to clean date prompt

This task is open-ended debugging. The acceptance criterion is unambiguous: piping `1-1-81` to the binary produces the kernel banner and accepts the date without crashing.

**Files:**
- Modify: any `host/*.c` as needed
- Possibly create: `asm/kernel.patches.asm` additions (NASM-syntax-only) if NASM mis-assembled something

- [ ] **Step 1: Diagnostic logging in BIOS dispatch**

Add an environment-variable-controlled trace:
```c
/* in bios_dispatch.c */
static int trace = 0;
__attribute__((constructor)) static void init_trace(void) {
    trace = getenv("DOS_TRACE") != NULL;
}
/* in dispatch entry: */
if (trace) fprintf(stderr, "[bios] off=0x%02x ax=%04x bx=%04x ...\n", off, ...);
```

- [ ] **Step 2: Identify the failure mode**

Run with trace: `DOS_TRACE=1 echo "1-1-81" | ./build/dos_host.exe 2>trace.log`. Read `trace.log`.

- [ ] **Step 3: Fix forward**

Common fixes:
- **CS:IP wrong:** adjust `emu_set_cs_ip` (Step 1 of Task 7)
- **RETF stack synthesis:** ensure the trap simulates RETF correctly (pop IP, then CS, from emu stack)
- **Carry flag not set:** disk error path needs CF=1 returned via emu_set_carry
- **Kernel reads garbage from BIOSREAD:** `disk_image_init_empty_320kb` might not have run before kernel boot, OR the kernel reads from a sector our mock geometry doesn't cover
- **Kernel `MEMSCAN` walks past EMU_MEM_SIZE:** the kernel probes for top-of-memory via write-verify; if our buffer is 96 KB but kernel scans up to 1 MB, it'll wraparound or write garbage. Diagnose with a guard page (extra 4 KB after EMU_MEM_SIZE filled with sentinel; check sentinel after run). Fix via `kernel.patches.asm` adding a `%define MEMSCAN_LIMIT` and patching the loop bound, OR by extending our buffer (96 KB → 256 KB).
- **NASM mis-assembled an instruction:** rare but possible. Find the discrepancy by disassembling `kernel.bin` (`ndisasm -b 16 build/kernel.bin | head -100`) and comparing against `86DOS.ASM` for that label.

- [ ] **Step 4: Acceptance test**

```
echo "1-1-81" | ./build/dos_host.exe 2>&1 | tail -10
```

Expected last lines (ignoring the "[host] ..." diagnostics):
```
86-DOS  version 1.00  (C) 1981  Seattle Computer Products
Enter today's date (m-d-y): 1-1-81
A>
```

(or whatever prompt the kernel jumps to after init — likely none if we haven't loaded SHELL.COM yet; emulator may halt or loop after kernel attempts to load the first transient. **That's fine for this plan** — Plan 2 adds SHELL.COM. Acceptance for Plan 1 is just: kernel reaches the date prompt, accepts the date, prints the banner.)

If the kernel attempts to read SHELL.COM after the date and fails: that's success for Plan 1. Capture the failure mode in the commit message; Plan 2 will start there.

- [ ] **Step 5: Commit & tag**

```bash
git add -A
git commit -m "host: kernel boots to date prompt; emulator + BIOS pipeline verified"
git tag -a host-kernel-boot -m "Plan 1 complete: 86DOS.ASM boots in emulator on host"
```

---

## Notes for the implementing engineer

- **The kernel is read-only.** Never edit `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM`. NASM-syntax adjustments live in `asm/kernel.patches.asm` as `%define`s / `%macros` and `%include` the original.
- **Each kernel patch must be syntactic, never logical.** A patch that changes assembled bytes for the same source line is fine (e.g., `DS 3` → `RESB 3`). A patch that re-routes control flow or rewrites a routine is forbidden — the project's "build from Tim Paterson's source" pitch depends on this.
- **8086tiny is vendored.** Modifications to `third_party/8086tiny/8086tiny.c` are allowed; tag each with `/* PATCH (host): reason */` for grep.
- **`DOS_TRACE=1` is your friend.** Most debugging time will be spent reading what the kernel is asking the BIOS for and figuring out why our reply is wrong.
- **Memory budget on host is irrelevant.** We have gigabytes; allocate generously if it helps. The 96 KB cap applies to the ESP32 build (Plan 3).
