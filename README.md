# espDos

Tim Paterson's actual MS-DOS 1.0 (1981), running unmodified on a $20
ESP32-S3 dev board.

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

## What you need

- **LilyGO T-Display-S3** (ESP32-S3 with 8 MB octal PSRAM, 16 MB flash).
  The OLED isn't wired up on this particular board, so all I/O goes
  through USB-Serial-JTAG.
- **ESP-IDF v5.4.1** (older versions don't ship the right NASM / esp_psram
  configuration; `~/esp/v5.4.1/esp-idf/export.ps1` on Windows).
- **NASM** for assembling the kernel and transients.
- **Python 3** (use `py` on Windows) for the FAT12 image builder.
- **MinGW-w64 GCC** for the host test suite.
- *(optional)* **QEMU 9.2.2** with octal PSRAM support
  (`esp-develop-9.2.2-20260417`) for fast iteration without flashing —
  see `docs/integrity.md` for the recipe and the binary URL.

## Build and flash

```powershell
# Build the kernel + boot stub + every transient + the FAT12 disk image.
bash asm/build_kernel.sh
py tools/build_disk.py build/disk.img

# Build the firmware. Default loads MANDEL directly. Pick another
# program with -DESPDOS_LOADER_<NAME>=1 (HELLO, COUNT, MANDEL, JULIA,
# or SHELL).
cd firmware
C:\Users\zombo\esp\v5.4.1\esp-idf\export.ps1
idf.py fullclean
idf.py build -DESPDOS_LOADER_SHELL=1
idf.py flash monitor
```

Inside `idf.py monitor`:

- Boot log → kernel banner → date prompt (auto-fed `1-1-80`) → SHELL menu
- Type `1`/`2`/`3`/`4` and Enter to launch; `q` to halt
- After a child program returns, SHELL re-enters and the menu reprints
- `Ctrl-]` exits the monitor; `Ctrl-T Ctrl-R` resets the board

## Build flags

| Flag                          | Effect                                        |
|-------------------------------|-----------------------------------------------|
| `-DESPDOS_LOADER_HELLO=1`     | Boot directly into HELLO.COM                  |
| `-DESPDOS_LOADER_COUNT=1`     | Boot directly into COUNT.COM                  |
| `-DESPDOS_LOADER_MANDEL=1` *(default)* | Boot directly into MANDEL.COM        |
| `-DESPDOS_LOADER_JULIA=1`     | Boot directly into JULIA.COM                  |
| `-DESPDOS_LOADER_LIFE=1`      | Boot directly into LIFE.COM                   |
| `-DESPDOS_LOADER_SHELL=1`     | Boot into the interactive menu                |
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
