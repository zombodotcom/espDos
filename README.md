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

- Spec written, approved
- Plan 1 (host kernel boot) written, not yet executed
- Plan 2 (live source-annotated browser pane on host) sketched in the
  roadmap doc, not yet expanded into a task plan
- Plans 3–5 (ESP-IDF port → full hardware demo → SHELL.COM and EDLIN)
  outlined in the roadmap; specifics deferred until earlier plans land

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
