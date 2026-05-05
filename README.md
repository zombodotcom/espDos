# espDos

Boot Tim Paterson's original `86DOS.ASM` — assembled from the source listings
he released in April 2026 — on a plain ESP32-WROOM-32, served to your browser
over WiFi.

The differentiating feature: a side-by-side view of Paterson's actual ASM
source with the currently-executing line highlighted, live, as the kernel
runs. You're not watching an emulator — you're watching 1981 code execute.

## Where to start (cloning fresh)

If you just cloned this repo, read in this order:

1. **`docs/superpowers/specs/2026-05-04-esp32-dos-port-design.md`** — the
   architecture (8086 emulator on ESP32, BIOS as far-call traps, browser
   terminal, flash-baked FAT12, etc.). Single source of truth for what
   we're building.
2. **`docs/research/2026-05-05-roadmap-and-cool-factor.md`** — the
   "what makes this distinctive vs. what's been done" research and the
   5-plan roadmap. Read this to understand the *why* behind the
   architecture and the live-source-annotation feature.
3. **`docs/superpowers/plans/2026-05-04-host-kernel-boot.md`** — Plan 1
   of 5: assemble the kernel with NASM, run it on host (Windows) in an
   adapted `8086tiny`, drive it to the date prompt with stdio BIOS.
   This is the next concrete thing to execute.

## Branches

- **`master`** — initial scaffold; superseded by `emulated-kernel`. Kept
  for history.
- **`emulated-kernel`** — active branch. Current architecture (kernel
  runs in 8086 emulator). All current docs and roadmap live here.
- **`plan1-kernel-host-verify`** — abandoned earlier attempt to use
  jgbarah's C translation of the kernel. Hit multiple kernel-flow bugs
  in core file I/O. Tagged `archive/plan1-translation-attempt` so
  `git checkout archive/plan1-translation-attempt` shows the dead end and
  the bugs we found. Read its commit log if you wonder why we pivoted to
  emulation.

## Status (as of 2026-05-05)

**Plan 1 progress: Tasks 1–3 of 8 done.**

- ✅ **Task 1: project skeleton + Makefile** — `Makefile`, `asm/build_kernel.sh`,
  `host/main.c` placeholder. `mingw32-make` produces `build/kernel.bin`
  + (eventually) `build/dos_host.exe`.
- ✅ **Task 2: assemble 86DOS.ASM** — `asm/scp_to_nasm.py` translates SCP-ASM
  2.43 dialect to NASM (22 mechanical rules; Tim Paterson's source remains
  read-only). Output: `build/kernel.bin` (5,861 bytes). Banner string
  "86-DOS" verified at offset 0x1d, "Copyright" at 0x32, first instruction
  is JMP DOSINIT at offset 0x146d. **The kernel assembles cleanly.**
- ✅ **Task 3: vendor 8086tiny** — Adrian Cable's emulator copied to
  `third_party/8086tiny/` (MIT). Two `/* PATCH (MinGW): ... */` patches
  applied so it compiles cleanly under MinGW-w64 GCC 15: (a) include
  `<conio.h>` and `<io.h>` on `_WIN32` for `kbhit`/`getch`/`open`/etc.,
  (b) replace one K&R-era function-pointer cast with a properly-typed
  one. Standalone compile produces `/tmp/8086tiny.o` with no warnings.
- ⏳ **Task 4: emulator adapter + far-call trap** — not yet wired.
  Two viable paths from here:
  1. **Edit 8086tiny.c in place**: replace its `main()` (which boots from
     a floppy image and uses an external BIOS file) with our own that
     loads `kernel.bin` at `KERNEL_SEG:0000`, sets CS:IP to that, and
     adds a per-instruction hook in the existing for-loop to detect
     `CS == BIOSSEG (0x40)` and dispatch into our BIOS handlers.
     Pro: keeps the well-tested instruction decoder. Con: invasive edits
     to vendored source.
  2. **Write our own minimal emulator**: ~1500–2500 lines of C
     implementing the 8086 instruction subset 86DOS uses. Cleaner
     integration; full control. Con: weeks of careful work; risk of
     ISA bugs that EDLIN/DEBUG would later trip.
  Recommend path 1 — start by `#ifdef`'ing out 8086tiny's `main()` and
  adding a `step()` callable that runs one iteration of the existing
  loop body.
- ⏳ **Tasks 5–8**: BIOS dispatch + console + disk handlers; load
  kernel into emu memory; iterate until banner + date prompt work.

### Hardware

The user has a **LilyGO T-Display-S3** plugged in (broken TFT, but we're
using browser anyway). The chip is on COM3 at VID:PID `303A:1001`. We
can't talk to it via esptool without a manual BOOT+RESET, so all
development is host-only until the user is back at the device. Good
news: T-Display-S3 has 8MB PSRAM and 16MB flash, so the memory budget
in the spec (which assumed plain ESP32-WROOM-32) has lots of slack.

### Where to pick up

After cloning and `git pull`:
1. Check `mingw32-make` produces `build/kernel.bin` (Task 1+2 work)
2. Adapt `third_party/8086tiny/8086tiny.c` per Task 4 of the plan; the
   kernel binary is ready to feed into it.

## Hardware target

Plain ESP32-WROOM-32. No PSRAM. No SD card. WiFi built-in. No external
peripherals. The whole demo runs through the browser over WiFi.

## Toolchain assumed

- **NASM** for the kernel (and `SHELL.COM` later)
- **MinGW-w64 GCC** for the host build (Windows)
- **ESP-IDF** for the embedded build (later plans only)
- **Python 3** for the disk-image and line-index build helpers

## License notes

- The kernel sources we assemble live in
  `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` (a sibling
  repo, cloned separately from `https://github.com/DOS-History/Paterson-Listings`)
  and are reproduced for historical study under their original copyright.
- Adrian Cable's `8086tiny` (vendored later in `third_party/8086tiny/`)
  is CC0 / public domain.
- This project's own code (host harness, BIOS handlers, web terminal,
  build tooling) is unlicensed in v1; pick a license before publishing.
