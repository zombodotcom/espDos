# espDos

Tim Paterson's actual MS-DOS 1.0 (1981), running unmodified on a $20
ESP32 dev board. Two targets supported today: the **LilyGO T-Display-S3**
(USB-Serial-JTAG console only) and the **LilyGO T-Dongle-C5** (the same
console *plus* an onboard 0.96" ST7735 LCD that mirrors output as a
status bar and an 80-column subpixel-rendered scrolling log — true DOS
column count on a 160-pixel display).

The kernel binary is built straight from `86DOS.ASM` — Paterson's own SCP
listings, mechanically translated to NASM by `asm/scp_to_nasm.py`. It runs
inside an Adrian Cable `8086tiny` fork on the chip, talks to a tiny BIOS
shim that traps far-calls to `BIOSSEG` (0x0040) for console + disk, and
loads its own `.COM` files off a FAT12 image flashed into a partition.

It boots, prints the original banner, accepts the date prompt, and
hands control to whichever transient you picked. Six ship today:

| Program     | Bytes | What it does                                      |
|-------------|------:|---------------------------------------------------|
| `HELLO.COM` |   232 | Banner box                                        |
| `COUNT.COM` |    71 | Prints 1..50 in decimal (AAM / DIV demo)          |
| `MANDEL.COM`|   450 | Q4.12 fixed-point ASCII Mandelbrot, 78×24         |
| `JULIA.COM` |  1082 | 16-color animated Julia set, c walks a circle    |
| `LIFE.COM`  |  1029 | Conway's Game of Life, 80 generations, colored   |
| `SHELL.COM` |   443 | Interactive menu; re-entry on `INT 20h` from kids |

Everything happens inside the unmodified 86-DOS kernel — `INT 21h`, the
1981 DISPATCH table, the original FAT12 file ops. The shell is itself
just another `.COM` file on disk; the kernel's loader brings it up the
same way it would any program.

## What it looks like

Boot, banner, date prompt (auto-fed `1-1-80`), shell menu — byte-for-byte
captured from a host run of the same kernel + transients:

```
86-DOS version 1.00
Copyright 1980,81 Seattle Computer Products, Inc.
Enter today's date (m-d-y): 1-1-80

espDos shell
  1) HELLO   - banner
  2) COUNT   - 1..50
  3) MANDEL  - ASCII fractal
  4) JULIA   - color animated set
  5) LIFE    - Conways Game of Life
  q) quit
> _
```

`1` runs `HELLO.COM`:

```
+----------------------------------------+
|  Hello, World!                         |
|  This is HELLO.COM running on espDos:  |
|  Tim Paterson 86-DOS 1.00, on ESP32-S3 |
+----------------------------------------+
```

`2` runs `COUNT.COM`:

```
01 02 03 04 05 06 07 08 09 10
11 12 13 14 15 16 17 18 19 20
21 22 23 24 25 26 27 28 29 30
31 32 33 34 35 36 37 38 39 40
41 42 43 44 45 46 47 48 49 50
```

`3` runs `MANDEL.COM` — Q4.12 fixed-point Mandelbrot, 78 × 24 ASCII,
ramp `' .:-=+*#%@'`. Cardioid + period-2 disk early-reject means the
big black blob is detected without iterating; everything else escapes
under the 24-iter cap and gets a density character. Render time on
hardware: ~1.2 s after the perf flags.

```
                ...@.....:.:::-:::..........-:::...:*::::=+=-+#::..:-::.....:.
             --++..-:-.::..:*::........:::-::....--::==:--*@+#*==::...::::....
           .+..:@:+:.:.:=::........*:--:+.....::::+:-*+@@@@@@@++#:::::-...:-..
         ..::---::%:.:.+........:+@-:.....=::+-:::-:###@@@@@@@@+-:+::#:::=:..:
       .:.:::.%.---:.........::......+:::=-*##@=--#@+@+*@@@@@@*##=*@==:::-#::.
     .::::.::=.................::-:-::::-::==#@@@@@@@@@@@@@@@@@@@@@@@+@@@@@+::
    .:+.:-...............:=*:=:::::::+:::-@=+#@@@@@@@@@@@@@@@@@@@@@@@@@@@##:::
   +:..........*=...-*:+:::::::::::@--:+-+@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@=---
  +.....:-::#...---:=:+=+=+=-*@+------==+@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%
 ...:.:=-::..:+:=::-:=:==*@@@@@@@@@%+==*@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@++=
 .:::::....-::::::-=-*++@@@@@@@@@@@@@@##@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@+-:
 ..:....:*--::-+:--+#@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@++::
.+=*+*@@##*#%*#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%+-=%=:
 ..-+...::#*::-+:--+#%@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@+:::
 .+::-=....-::::::-+-==@@@@@@@@@@@@@@@##@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@#+:
 ...:.:+=:...::=:::*:+:==*@@@@@@@@@%+=++@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@*==
  =.....::+:#...:=::::-*+*+=-*@#=-----+%+@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@=
   :.:.........#....:=:*-::::::::::+++:+-*@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@==-:
    ::=.+...............=:::::::::::--:::=@@+#@@@@@@@@@@@@@@@@@@@@@@@@@@@#+:::
     .::-:.:-:................*:-=:::::--::==#@@@@@@@@@@@@@@@@@@@@@@@+@@@@##::
       .::#::*=::+:..........:-......:=:::-@#%@+--+@#@#*@@@@@@@#@=@#==:::-@=:.
         ..-=:-:::--.::.........-::+......=::==::-+:@++@@@@@@@@+-:+:+::::::..-
           @:..-=-=:.---:::........-:--::.....+:::::+=+@@@@@@@+#-::::--...:+..
             ::-+..-::.::..::#:........::::::....==:::-:-++@+*@=-:-:..=:::....
```

`4` runs `JULIA.COM` — same Q4.12 IMUL kernel, but `c` walks a 30-step
circle of radius 0.7885 around the origin while `z₀` varies per pixel.
Each pixel emits an `ESC[NNm` ANSI color escape + density char (~6
bytes/pixel); frames are separated by `ESC[H` (cursor home) so a real
terminal overwrites the previous frame in place. The black filaments
of a dendrite Julia morph through dust and seahorse atlases as the
animation walks the c-orbit — markdown can't show the color, but you
can imagine the same shape as MANDEL above with a per-pixel hue ramp
that morphs frame-to-frame.

`5` runs `LIFE.COM` — Conway's Game of Life on a 78 × 24 toroidal
grid, 200 generations. The seed is a 16-bit LCG'd ~25% density (about
470 alive cells at gen 0) so each run starts from real chaos. Cells
are encoded as long-dead / just-died / long-alive / newborn so the
render layer can show *gray dots* for dying cells, *bright white
hashes* for newborn ignition, and a *per-generation cycling color*
for the established population. The result is a pulse that you can
visually track through gliders, oscillators, and the chaotic decay
of dense regions.

After any of the children does `INT 20h`, control returns to SHELL
(via an IVT[20h] swap installed before launch) and the menu reprints
— `q` halts cleanly.

## What you need

**Board** — pick one:

- **LilyGO T-Display-S3** (ESP32-S3, 8 MB octal PSRAM, 16 MB flash).
  The OLED isn't wired up on this board, so all I/O goes through
  USB-Serial-JTAG.
- **LilyGO T-Dongle-C5** (ESP32-C5, 8 MB quad PSRAM, 16 MB flash, 0.96"
  ST7735 LCD). USB-Serial-JTAG console *plus* the LCD mirrors BIOS
  output as a status bar + 80-col subpixel-rendered log.

**Toolchain** (same for both targets):

- **ESP-IDF v6.0.1** — install via the new IDF Installation Manager
  (defaults to `C:\esp\v6.0.1\esp-idf\` on Windows; source `export.ps1`
  before invoking `idf.py`). v5.4.x will not work — the C5 target,
  Picolibc, the new component graph, and the warnings-as-errors default
  all want v6.x.
- **NASM** for assembling the kernel and transients (Windows: `scoop
  install nasm`).
- **Python 3** (use `python`, not `py` — the Windows Python launcher
  often misbehaves on Scoop-managed Python).
- **MinGW-w64 GCC** for the host test suite.
- **Paterson-Listings** is now a submodule. Clone with
  `--recurse-submodules` or run `git submodule update --init` after
  a plain clone — `asm/build_kernel.sh` reads `86DOS.ASM` from there.
- *(optional)* **QEMU 9.2.2** with octal PSRAM support
  (`esp-develop-9.2.2-20260417`) for fast iteration without flashing —
  see `docs/testing.md`.

## Build and flash

Step 1 — build the kernel image (target-agnostic). Run once after a
fresh clone or any time you edit something under `asm/`:

```bash
bash asm/build_kernel.sh
python tools/build_disk.py build/disk.img
```

Step 2 — build the firmware. ESP-IDF picks the per-target overlay
(`firmware/sdkconfig.defaults.esp32{s3,c5}`) automatically based on
`set-target`.

```powershell
& 'C:\esp\v6.0.1\esp-idf\export.ps1'
cd C:\Users\zombo\Desktop\Programming\espDos\firmware

# T-Display-S3:
idf.py set-target esp32s3
idf.py fullclean
idf.py build flash monitor

# OR T-Dongle-C5 (different USB device — pick the right COM port):
idf.py set-target esp32c5
idf.py fullclean
idf.py build flash monitor
```

The default boot is **SHELL.COM** — interactive menu over USB-Serial-JTAG.
Type `1`-`5` to launch HELLO / COUNT / MANDEL / JULIA / LIFE; after each
child returns via `INT 20h`, SHELL reprints the menu. `q` halts cleanly.

`Ctrl-]` exits the monitor; `Ctrl-T Ctrl-R` resets the board.

## What the C5 LCD shows

In landscape (160 × 80) — top row is a sharp 26-col status bar (program
name + heartbeat counter), the rest is an 80-col subpixel log. ANSI
escapes are stripped before rendering so the log shows clean text;
the USB-Serial-JTAG side still gets the full ANSI stream so JULIA and
LIFE animate correctly there. Glyphs use Bowman/ClearType-style
subpixel rendering — color fringes on edges are expected, that's how
80 columns fit on 160 pixels.

```
+-----------------------------------------------------+
| SHELL.COM       b00012345                           |  <- sharp 26-col status bar
+-----------------------------------------------------+
| 86-DOS version 1.00                                 |  <- 80-col subpixel log
| Copyright 1980,81 Seattle Computer Products, Inc.   |     (color fringes on glyph
| Enter today's date (m-d-y): 1-1-80                  |      edges expected — that's
|                                                     |      the subpixel rendering
| espDos shell                                        |      working)
|   1) HELLO   - banner                               |
|   2) COUNT   - 1..50                                |
|   3) MANDEL  - ASCII fractal                        |
|   4) JULIA   - color animated set                   |
+-----------------------------------------------------+
```

Toggle off with `idf.py menuconfig` → "espDos display" → uncheck
"Use subpixel-rendered 80-column log" if you'd rather have crisp
26-col text without color fringes.

## Build flags

| Flag                          | Effect                                        |
|-------------------------------|-----------------------------------------------|
| `-DESPDOS_LOADER_HELLO=1`     | Boot directly into HELLO.COM (skip SHELL)     |
| `-DESPDOS_LOADER_COUNT=1`     | Boot directly into COUNT.COM (skip SHELL)     |
| `-DESPDOS_LOADER_MANDEL=1`    | Boot directly into MANDEL.COM (skip SHELL)    |
| `-DESPDOS_LOADER_JULIA=1`     | Boot directly into JULIA.COM (skip SHELL)     |
| `-DESPDOS_LOADER_LIFE=1`      | Boot directly into LIFE.COM (skip SHELL)      |
| `-DESPDOS_LOADER_SHELL=1`     | Explicit SHELL.COM (default; flag is a no-op alias) |
| `-DESPDOS_AUTOPICK=N`         | Pre-feed digit N to SHELL (for QEMU testing)  |
| `-DESPDOS_INTERACTIVE_DATE=1` | Type the date yourself instead of auto-feed   |
| `-DESPDOS_LOG_OUT=1`          | Mirror BIOSOUT through `ESP_LOGI` (QEMU debug)|
| `-DESPDOS_HEARTBEAT=1`        | Per-beat instruction-count log (debug)        |

## Architecture

```
+--------------------------------------------------+
|  app_main (firmware/main/espdos.c)               |
|    - allocates 1MB+64KB emu RAM in PSRAM         |
|    - copies bootstub + kernel + 8086tiny BIOS    |
|    - sets CS:IP = BOOT_SEG:0  and runs           |
+--------------------------------------------------+
                      |
                      v   per-instruction loop in
+--------------------------------------------------+
|  esp8086.c  (forked 8086tiny, MIT)               |
|    - 1MB+64KB mem[] in PSRAM via EXT_RAM_BSS     |
|    - REGS_BASE = 0xF0000                         |
|    - per-step trap on  CS == 0x0040  (BIOSSEG)   |
+--------------------------------------------------+
                      |
                      v   BIOS far-call dispatch
+--------------------------------------------------+
|  bios.c                                          |
|    - STAT/IN/OUT  -> USB-Serial-JTAG             |
|    - READ/WRITE   -> esp_partition (dos_disk)    |
|    - BIOSSEG handlers RETF on return             |
+--------------------------------------------------+
                      |
                      v   86-DOS kernel runs as 8086
+--------------------------------------------------+
|  kernel.bin  (assembled from 86DOS.ASM)          |
|    - 6,341 bytes of Tim Paterson's 1981 source   |
|    - INT 21h DISPATCH, FAT12, BUFIN, PRTBUF...   |
|    - JMP FAR USER_SEG:0x100  hands off transient |
+--------------------------------------------------+
```

The integrity argument — what's original Paterson code vs. what's
espDos's wiring — is laid out file-by-file in `docs/integrity.md`.
The Mandelbrot performance work and the literature survey on 1980s
fractal optimization are in `docs/mandelbrot-performance.md`.

## Tests

```bash
cd tests/emu && mingw32-make run
```

9 host tests, each running the same `esp8086.c` the firmware uses,
exercising it from a small C harness with stub BIOS handlers:

- `test_emu_basic`     — opcode decoder + register file
- `test_memory_bounds` — full 1 MB span + REGS_BASE alignment
- `test_kernel_banner` — kernel runs to date prompt; banner byte-exact
- `test_loader`        — bootstub + loader load HELLO; "Hello, World!" appears
- `test_mandel`        — full 78×24 Mandelbrot grid renders
- `test_fininit_stack` — stack layout at FININIT exit (drove the loader design)
- `test_disk_*`        — FAT12 read paths

## Why this exists

You can find emulators that pretend to run DOS by interpreting
recompiled-from-source dialects, or by booting a stripped-down kernel
that calls the same INT 21h numbers. espDos boots the **original
binary**, instruction by instruction, on a $20 chip. The integrity
argument — that you can git-diff Paterson's 1981 listings against what
runs on the wire — is the entire point.

## License

- `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` — original
  copyright Tim Paterson / Seattle Computer Products, 1981. Reproduced
  for historical study under their original terms; not modified.
- `third_party/8086tiny/` — Adrian Cable, MIT. Forked into
  `firmware/components/esp8086/` with PSRAM + ESP-IDF integration
  patches that are clearly marked.
- espDos's own code (asm transients, BIOS shim, build tooling, docs)
  is unlicensed in v1 — file an issue if you want to use it.
