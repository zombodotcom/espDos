# Plan: ESP32 DOS port — make it actually cool

## Context

User is building: assemble Tim Paterson's `86DOS.ASM` (released April 2026 in `DOS-History/Paterson-Listings`), run it on a plain ESP32-WROOM-32, expose via browser terminal over WiFi.

User pushed back on "just some emulation or whatever idk" and asked what people would actually find cool. Research across three angles converged:

- **Prior art (closest competitors):** FabGL and Pico-286 both run DOS on microcontrollers but use VGA+PS/2, never a browser. PCjs/js-dos run DOS in browser but on desktop, not microcontroller. **Nobody has shipped a "build 86-DOS from Paterson's source + run on a microcontroller + serve to a browser" project.** That intersection is genuinely open. The Paterson listings dropped 3 weeks ago; community projects haven't appeared yet, but the window won't stay open long.
- **What goes viral in retro embedded** (Pico-286, FRANK OS, FabGL): immediate "open it, it works" demos with a usable thing on the screen + a strong writeup. "Another emulator" without a hook gets 50–150 HN points; "watch DOS execute, instruction by instruction, in your browser" hits the share-worthy moment.
- **Alternative architectures evaluated:** real-8086 chip pairing (40–60h, fragile bus timing), FPGA soft 8086 (100–160h, Verilog learning curve), manual clean-room translation (400–500h, painful), recursive Paterson-assembler-builds-itself (cute story, no demo benefit). **All add weeks for marginal coolness gain over software emulation + a great UI.**

Independent recommendation across all three research agents: **keep software emulation, but add a live source-annotated browser view as the headline feature.** This is the differentiator that takes the project from "another DOS emulator" to "interactive 1981-code archaeology." It also doubles as the project's own debugger, which makes the rest of the roadmap easier.

## Recommended approach

Architecture stays as the existing spec describes (emulator + BIOS far-call traps + flash FAT12 + WebSocket terminal), with **one structural addition**: a second WebSocket endpoint and a browser side-pane that highlights Tim Paterson's actual `86DOS.ASM` source line currently being executed by the emulator, in real time, with optional pause/step controls and a register sidebar.

The user's pitch becomes: "**Watch Tim Paterson's 1981 code execute, line by line, in your browser, on a $5 chip.**"

### Build-time pipeline

NASM `-l kernel.lst` already produces a per-byte source-line mapping. A small Python tool (`tools/build_lineidx.py`) post-processes it into a compact RLE+page-directory binary index (~10–14 KB total). Source text (~180 KB) is also extracted as UTF-8 line-numbered text. Both flash to a dedicated `dos_assets` partition (256 KB).

### Runtime data path

A FreeRTOS timer fires at ~60 Hz, setting a flag the emulator dispatch loop checks once per instruction (~5 ns cost, predicted-not-taken branch). When set, snapshot `(CS, IP, AX, BX, CX, DX, SP, BP, SI, DI, FLAGS, in_kernel)`, resolve current source line via `mmap`'d index lookup (~1 µs), enqueue to a 4-entry SPSC ring drained by `net_task`. Push to browser as a 24-byte binary WebSocket frame on `/dbg` (separate from the existing `/tty` terminal channel).

### Browser side

Split-pane layout: xterm.js terminal on the left (~70 %), source pane on the right with a `<span data-line=N>` per line. Incoming sample → remove `.cur` class from prior span, add to new span, `scrollIntoView` if not visible. Register sidebar updates from same sample. Pause / step / play buttons send JSON commands on the same WebSocket. Source text fetched once at connect via HTTP `GET /source`.

### Pause/step protocol

Atomic `g_run_state` enum (`RUNNING | PAUSE_REQ | PAUSED | STEP_ONE`) read once per instruction. On pause request, emulator transitions cleanly between instructions (never mid-syscall — BIOS handlers complete synchronously inside one step), sends a `paused-ack` sample. On resume, just keeps going. Round-trip ~5–20 ms on local WiFi.

### Memory budget impact

~3 KB DRAM (sample ring + WS buffers + small page-directory cache). Index and source live in flash via `mmap` — zero RAM cost. Existing 50–90 KB slack untouched.

### Roadmap reorder

The live-source view goes in as **Plan 2**, before any ESP-IDF work. Three reasons:

1. **It's the project's best debugger.** Plans 3–4's open-ended kernel-boot debugging (the "iterate to date prompt" task in current Plan 1; subsequent ESP32 issues) becomes drastically easier when you can SEE which kernel line is hung. Build the debugger early, use it for everything.
2. **Validates the lineidx pipeline before adding embedded complexity.** NASM listing format edge cases (cross-file `%include` line numbers) get debugged on Windows with Python tooling, not on a microcontroller.
3. **It's the demo.** Every subsequent screenshot/screencast of the project should show the source pane. Building the visual identity early benefits the eventual writeup.

Updated plan sequence:

| Plan | Status | Goal |
|------|--------|------|
| 1 | exists (`2026-05-04-host-kernel-boot.md`) | Kernel boots in 8086 emulator on Windows; stdio console; date prompt accepts input |
| **2** | **new (write next)** | Host source-view pane: kernel running on Windows + browser served by a small local WebSocket bridge; live source highlight + registers + pause/step |
| 3 | (was 2) | ESP-IDF port: emulator + BIOS + flash disk + WiFi + `/tty` WebSocket. UART-only fallback. |
| 4 | (was 3) | ESP32-side `/dbg` WebSocket + `dos_assets` partition + browser source pane on hardware |
| 5 | new | `SHELL.COM` in 8086 ASM + run a real `.COM` (EDLIN target) |

### Spec update

The existing spec at `esp-dos/docs/superpowers/specs/2026-05-04-esp32-dos-port-design.md` should be updated to promote the source-annotated view from "neat idea" to a v1 goal. New goal line: "Browser displays Tim Paterson's actual `86DOS.ASM` source with the executing line highlighted live alongside the terminal." Non-goals stay the same. Risks gain a row for "NASM listing line-number cross-file resolution" with the mitigation (re-resolve `kernel.patches.asm` lines back to `86DOS.ASM` lines via the listing's filename column).

## Critical files

**New (Plan 2 territory):**
- `esp-dos/tools/build_lineidx.py` — parses NASM `-l` output → compact RLE index + source.txt
- `esp-dos/tools/build_assets.py` — bundles index + source for partition flashing
- `esp-dos/host/lineidx.h`, `host/lineidx.c` — pure-data lookup `u32 line_for_pc(u32)`; loads from file on host, mmaps on ESP32
- `esp-dos/host/dbg_trace.h`, `host/dbg_trace.c` — sample ring, run-state machine, `dbg_capture()` per sample
- `esp-dos/host/ws_bridge.c` (host-only Plan 2) — local WebSocket server bridging host emulator to browser; loopback only
- `esp-dos/web/index.html` — split-pane scaffold
- `esp-dos/web/term.js` — xterm.js wiring (also used in Plan 4)
- `esp-dos/web/dbg.js` — `/dbg` WebSocket client; highlight + register + controls
- `esp-dos/web/source.css` — pane layout, `.cur` highlight, register grid

**Modified:**
- `esp-dos/host/emu_8086.c` — one-line sample-flag check + run-state poll in dispatch loop. Kept zero-cost on the hot path (single byte test).
- `esp-dos/host/main.c` — initialize lineidx; wire dbg_trace; spawn ws_bridge thread
- `esp-dos/Makefile` — add `build/lineidx.bin`, `build/assets.bin` targets that depend on `build/kernel.lst`
- `esp-dos/asm/build_kernel.sh` — emit `kernel.lst` with NASM `-l`
- `esp-dos/docs/superpowers/specs/2026-05-04-esp32-dos-port-design.md` — promote source-view to v1 goal

**Reused:**
- Existing `kernel.bin` build (Plan 1 Tasks 1–2)
- Existing `8086tiny` adapter pattern from Plan 1 Tasks 3–4 — the only new emulator code is the sample-flag check
- Existing BIOS dispatch + console + disk handlers from Plan 1 Tasks 5–6 — the `/dbg` channel is additive, doesn't change BIOS at all
- Existing `host/main.c` driver — extended, not rewritten
- xterm.js (vendored or from CDN) — same library both Plan 2 (host) and Plan 4 (ESP32) use

## Verification

**Plan 2 acceptance test (host):**
1. Build: `mingw32-make` produces `build/kernel.bin`, `build/lineidx.bin`, `build/source.txt`, `build/dos_host.exe`.
2. Run: `./build/dos_host.exe` (no args) — boots the kernel and starts a local HTTP+WS server on `localhost:8080`.
3. Open `http://localhost:8080` in any browser. Verify within 5 seconds:
   - Terminal pane shows the 86-DOS banner
   - Source pane shows `86DOS.ASM` with a highlighted line that *moves* as the kernel executes
   - Register sidebar shows non-zero values updating
   - Highlighted line corresponds to a real instruction in `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` (spot-check by line number)
4. Type `1-1-81<Enter>` in the terminal pane. Kernel proceeds past the date prompt; source highlight tracks through the post-init code (MEMSCAN, FAT init, etc.).
5. Click pause: highlight freezes within ~50 ms. Click step: highlight advances by exactly one instruction. Click play: resumes.
6. Lineidx coverage check: log a warning if any sample's PC fails to resolve to a `86DOS.ASM` line. Should be near-zero (`kernel.patches.asm` lines resolve back to original via the listing's filename column).

**Plan 4 acceptance test (ESP32, deferred):** same as above, but device IP instead of localhost, over WiFi.

**Architecture sanity checks during Plan 2:**
- Emulator dispatch-loop benchmark before/after sample-flag addition. Cost should be unmeasurable (<1 % regression). If it's measurable, fall back to ISR-driven sampling per the Plan agent's mitigation.
- WebSocket bandwidth at 60 Hz × 24 B = ~1.4 KB/s sustained. Trivially within local Wi-Fi limits but worth confirming doesn't bottleneck on laptop loopback.
- DRAM footprint of new `dbg_trace` + `lineidx` page-directory: should be ≤4 KB.

## What we are explicitly NOT doing

- **Real 8086 chip pairing.** Researched (40–60 h, fragile bus timing, requires PCB). Coolness gain over software emulation + source view is marginal once the source view is in place. Defer to a v2 hardware-edition project if anyone wants it.
- **FPGA soft 8086.** Same reasoning (100–160 h, Verilog learning curve).
- **Manual clean-room translation of the kernel.** Already established as 400+ hours with hidden-bug risk. Emulation sidesteps the entire class of issues.
- **Multiplayer shared sessions.** Cool but high effort; defer to a v2 stretch.
- **Multi-version timeline (DOS 1.0 / 1.25 / PC-DOS dev).** We have all the listings; could be a future Plan 6 stretch. Not core to v1 identity.
