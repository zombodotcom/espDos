# espDos roadmap

What's next, in roughly the order it makes sense to build it. None of
this is committed to — it's a snapshot of options and tradeoffs so we
don't have to re-derive them every time we sit down with the project.

## Performance

Current per-instruction cost in the 8086 interpreter is ~50–80 native
cycles before any opcode work. That's spent on the BIOSSEG trap
check, opcode fetch, ModRM decode, and the 256-way switch on
`xlat_opcode_id` in `firmware/components/esp8086/esp8086.c:417`.

What's already done (latest commit):

- **Compiler optimization**: `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
  (`-O2`) instead of the default `-Og`. The big win on the inner
  switch is jump threading; gcc folds the per-case dispatch into a
  cleaner indirect jump table.
- **CPU frequency**: 240 MHz (was 160). Direct 1.5× on every native
  instruction.

Combined expected speedup: 2–2.5× over previous build. Eyeball
verification on hardware: MANDEL renders in ~1.2–1.5 s, JULIA frames
cycle every ~0.5 s.

What's *not* done and why, in order of remaining benefit:

1. **BIOSSEG trap pre-screening** (~15–25% additional). The current
   loop checks `regs16[REG_CS] == 0x0040` on every iteration. Most
   transients spend almost no time in BIOSSEG (one entry per AH=02
   CONOUT, ~80 instructions per BIOSSEG visit, so the trap check is
   useful only ~0.1% of the time but pays its cost on the other
   99.9%). A region-based optimization — "bypass the check until CS
   changes" — would skip it on the hot path. Risk: code churn in the
   single most carefully-tested file in the project. Defer until
   there's a concrete program that's still too slow at 2.5×.

2. **MEMSCAN runtime patch** (~715,000 instructions saved at boot).
   Explicitly *out of scope* — the kernel running Tim Paterson's
   bytes verbatim is the integrity argument the project is built
   around. Patching MEMSCAN at runtime would weaken that. The 715 K
   are paid once at boot (~2 s on hardware after Tier 1) and never
   again; long-running animations don't see them.

3. **Native compile (JIT/AOT)** (~5–20×). Translate hot 8086 basic
   blocks to Xtensa instructions on first execution. Major project;
   2–3 weekends.

## Classic DOS programs to add

The system has six transients today (HELLO, COUNT, MANDEL, JULIA,
LIFE, SHELL). The next interesting category is *interactive*
programs — anything that reads input mid-program-loop, not just at a
single prompt. The existing loader/transient/shell pattern handles
launch and exit; what's missing is the real-time input pattern.

| Program     | Est. asm | New primitive demonstrated                |
|-------------|---------:|-------------------------------------------|
| SNAKE       |     ~400 | BIOSSTAT polling + AH=07 in a game loop   |
| TYPE        |     ~150 | INT 21h OPEN/SEQRD/CLOSE; FCB API         |
| DIR         |     ~250 | INT 21h SRCHFRST/SRCHNXT; date formatting |
| TETRIS      |    ~1500 | rotation tables; score; blitting          |
| EDLIN-lite  |     ~800 | line editor; INT 21h BUFIN; file write    |
| MICRO BASIC |   ~2000+ | tokenizer + interpreter; biggest demo     |

**Recommend SNAKE next.** It's the simplest program that exercises
every primitive the others need: a game-loop that polls input
without blocking (BIOSSTAT to check, AH=07 to read), ANSI cursor
positioning to draw the snake without redrawing the whole grid each
tick, and a known-correct visual that lets us debug the input path
(if the snake doesn't turn when we press a key, the bug is local).

After SNAKE works, TYPE/DIR are easy and prove the FCB file API.
Those unlock anything that reads files. EDLIN-lite + MICRO BASIC
become buildable once we have OPEN/CREATE/WRITE primitives in
production code.

## Paths to graphics

The LCD chip on this T-Display-S3 board isn't wired up — the user
has the unit but the ribbon cable isn't connected. So immediate
graphics paths fall into two camps: **don't need the LCD** and
**need it before they work**.

### Don't need the LCD (do these first)

**Unicode block characters**, U+2580 range. A single character cell
holds four sub-pixels via `▘▝▖▗▀▄▌▐█` (Quadrant Block). 78×24 grid →
effective 156×48 — 4× resolution from the same kernel + same
console + same `bios_out`. Multibyte UTF-8 already passes through
our raw JTAG output. Cost per program: ~30 lines of asm to encode
2×2 pixel groups → block-character index. Visual win: roughly the
same as moving from 320×200 to 640×400 in the DOS world. Trivially
worth doing.

**Sixel / iTerm graphics protocol**. xterm with sixel support, or
iTerm2/WezTerm/Konsole, render images embedded in the terminal byte
stream. We could emit sixel-encoded framebuffers from the existing
CONOUT path. ~500 lines of asm for a sixel encoder. Doesn't help
inside `idf.py monitor` (not a graphical reader); user would need a
real terminal. Worth doing once Unicode subcells aren't enough —
true raster, no pretending.

### Need the LCD wired

**Native ST7789 framebuffer**. 320×170 SPI panel, framebuffer in
PSRAM (320×170×2 = 109 KB, easily fits). New BIOSSEG entries (e.g.
`BIOSPIXEL` at offset 0x1E, `BIOSBLIT` at 0x21) and a panel-flush
task. MANDEL/JULIA/LIFE swap their per-pixel CONOUT for per-pixel
BIOSPIXEL. Effort: 1–2 weekends after the wiring is fixed; the
wiring step gates everything else. This is the "real" path — once
running, the demo escapes the terminal entirely.

**Web UI streaming the framebuffer over Wi-Fi**. Original espDos
plan pre-pivot: HTTPD + WebSocket + JS canvas client + framebuffer
protocol. Largest effort. Cleanest UX: anyone with a browser sees
the screen. Worth considering if the LCD route turns out to be too
constraining (170-pixel-tall display is shorter than even 320×200
classic VGA), or as the "publicly demoable" deliverable.

### Recommended order

1. **Unicode subcells** in MANDEL/JULIA/LIFE, ~30 lines per program,
   no firmware changes. Unblocks immediate visual win.
2. **SNAKE** (interactive primitive). Without graphics, just ASCII.
3. **Wire the LCD** when the user is at the bench.
4. **Native ST7789 framebuffer** + new BIOSSEG entries.
5. **Web UI** if/when we want a publicly shareable demo.

## Things explicitly *not* on the roadmap

- Real DOS apps that require >1 MB or 32-bit code (DOOM,
  Wolfenstein 3D, Lemmings). The 8086 emulator's 1 MB address space
  rules these out.
- TSR (terminate-stay-resident) programs. 86-DOS 1.00 doesn't have
  the INT 27h hooks for them.
- Networking from the DOS side. The Wi-Fi stack lives in firmware
  and could be exposed via new BIOSSEG entries, but this is a big
  abstraction with no obvious classic DOS analog.
- Multitasking. Single foreground program, one INT 21h dispatcher,
  same as 1981.

## Reference: how to add a transient

For when this fades from short-term memory. To add a new program
called `FOO.COM`:

1. `asm/foo.asm` — origin 0x100, end with `int 0x20`.
2. Append three nasm invocations to `asm/build_kernel.sh`
   (FOO.COM build + `loader_foo.bin` + `bootstub_foo.bin`).
3. Add `("FOO     ", "COM", "build/foo.bin")` to the FILES list in
   `tools/build_disk.py`.
4. Add `bootstub_foo.bin` to EMBED_FILES in
   `firmware/components/kernel_blob/CMakeLists.txt`.
5. Add `if(ESPDOS_LOADER_FOO)` block to `firmware/main/CMakeLists.txt`.
6. Add the `#elif defined(ESPDOS_LOADER_FOO)` arm to the bootstub
   cascade in `firmware/main/espdos.c`.
7. Add `cmp al, 'N' / je pick_foo / jmp load_and_run` to
   `asm/shell.asm`'s dispatch, plus a menu line and `FOO_SECTOR equ
   N` constant. Sector number = (cluster × 1) + 9 in our FAT12 layout
   (cluster 2 = sector 11).

The shell's per-program sector-count override (CX=N when picking)
already supports multi-sector programs.
