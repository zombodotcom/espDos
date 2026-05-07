# How espDos is tested

Three layers, each catching different bugs, ordered fast → slow:

```
┌──────────────────────────────────────────────────────────────────┐
│ Layer 1: Host tests (tests/emu/*.c)                              │
│   - Compile esp8086.c with host gcc; iterate in ~1 second.       │
│   - Catches: emulator correctness, kernel behavior, transient    │
│     logic.                                                        │
│   - Misses: ESP-IDF integration, PSRAM, real BIOS handlers.      │
├──────────────────────────────────────────────────────────────────┤
│ Layer 2: QEMU (qemu-system-xtensa with octal-PSRAM patches)      │
│   - Run the actual flashed firmware image, ~5 seconds to boot.   │
│   - Catches: ESP-IDF integration, PSRAM init, partition table,   │
│     bios.c's USB-Serial-JTAG path (with ESPDOS_LOG_OUT=1).       │
│   - Misses: USB host timing, monitor's ANSI interpretation,      │
│     real keystroke input.                                        │
├──────────────────────────────────────────────────────────────────┤
│ Layer 3: Hardware (LilyGO T-Display-S3 + idf.py flash monitor)   │
│   - Final eyeball test, ~90 seconds per iteration.               │
│   - Catches: animation timing, ANSI color rendering, real input. │
│   - Misses: nothing, but is slow and physical.                   │
└──────────────────────────────────────────────────────────────────┘
```

Always start at Layer 1. Don't reach for Layer 3 until 1 and 2 are
green — it's 90× slower.

## Layer 1: Host tests

Location: `tests/emu/`. Ten `test_*.c` files plus `test_helpers.c` and
a `Makefile`.

The key trick: **`tests/emu/Makefile` compiles the exact same
`esp8086.c` the firmware uses** (`../../firmware/components/esp8086/esp8086.c`),
just with host gcc instead of xtensa-esp32s3-elf-gcc, and without
`ESP_PLATFORM` defined. The `EXT_RAM_BSS_ATTR` macro becomes a
no-op on host so `mem[]` lives in regular host memory; the
`KEYBOARD_DRIVER` macro stays in its UNIX form (`read(0, …)` →
`pc_interrupt(7)`); everything else is byte-identical to firmware.

Each test boots the bootstub + kernel + (optionally) a transient,
drives input via a `kernel_bios` callback the test installs, captures
output bytes into a buffer, and asserts on it.

| Test                       | What it asserts                              |
|----------------------------|----------------------------------------------|
| `test_jmp_near`            | opcode decoder + register file basics        |
| `test_kernel_first_step`   | first kernel instruction is `JMP DOSINIT`    |
| `test_bootstub`            | bootstub IVT setup, jump to kernel works     |
| `test_hello`               | the BIOS-confidence-harness `hello.bin` runs |
| `test_kernel_banner`       | "86-DOS version 1.00" prints byte-exact      |
| `test_memory_bounds`       | full 1 MB span + REGS_BASE = 0xF0000         |
| `test_fininit_stack`       | stack contents at FININIT exit (drove the loader design) |
| `test_loader`              | bootstub+loader load HELLO; banner appears   |
| `test_mandel`              | 78×24 Mandelbrot grid renders, ≥30% non-space|
| `test_julia`               | ≥2000 ANSI escapes captured + cursor-home present |

Each test ends by `printf`-ing `PASS\n` or `FAIL (n)\n`. The Makefile
runs all of them and prints `exit=0` after each one.

```
cd tests/emu
mingw32-make run | grep -E "^(===|PASS|FAIL|exit=)"
```

Expect 10 `PASS` lines, 0 `FAIL` lines, all `exit=0`. The full output
of `test_mandel` includes a human-readable Mandelbrot grid you can
eyeball — that's intentional, it lets you spot regressions visually
even when the assertions still pass.

### What host tests can't see

- **`firmware/components/bios/bios.c`** — host tests use their own stub
  `kernel_bios` callbacks instead of compiling bios.c (which depends
  on `usb_serial_jtag_*`, `esp_partition_*`, FreeRTOS, etc. that
  aren't available on host). So a bug introduced in bios.c — auto-feed
  string, escape-char filtering, JTAG buffering — won't trip a host
  test. Use Layer 2.
- **PSRAM allocation, partition table, octal-PSRAM init** — pure
  ESP-IDF concerns. Layer 2 catches them.
- **Compiler flag changes** (`-O2`, `-O3`) on the *xtensa* toolchain.
  Host tests use mingw gcc at default optimization. If `-O2` triggers
  latent UB in `esp8086.c`, only Layer 2 + 3 will catch it.

## Layer 2: QEMU

Location: `firmware/build/qemu_*.bin` files (gitignored).

QEMU runs the *real* firmware image (post-merge_bin), with the same
ESP-IDF stack, the same PSRAM emulation, the same `bios.c`, the same
SHELL/transients off the same FAT12 disk image. UART0 output is
captured to a log file we then grep.

### The QEMU binary

ESP-IDF v5.4.1 ships an older QEMU that *doesn't support S3 octal
PSRAM*. The version that works is at:

```
C:\Users\zombo\.espressif\tools\qemu-xtensa-new\qemu\bin\qemu-system-xtensa.exe
```

It's the `esp-develop-9.2.2-20260417` build. If it's not there,
download from
[github.com/espressif/qemu](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417).
The asset name (Windows): `qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-x86_64-w64-mingw32.tar.xz`.

### The recipe

```
# 1. Build firmware with the variant you want to test.
cd firmware
idf.py fullclean
idf.py build "-DESPDOS_LOADER_SHELL=1" "-DESPDOS_AUTOPICK=2" "-DESPDOS_LOG_OUT=1"

# 2. Merge bootloader + partition table + app + disk image into one
#    flat 8 MB flash image. The disk.img at 0x310000 IS critical —
#    without it, the kernel reads flash padding bytes as transient
#    code and execution diverges into garbage.
esptool.py --chip esp32s3 merge_bin -o build/qemu_test.bin \
    --flash_mode dio --flash_size 8MB --fill-flash-size 8MB \
    0x0      build/bootloader/bootloader.bin \
    0x8000   build/partition_table/partition-table.bin \
    0x10000  build/espdos.bin \
    0x310000 ../build/disk.img

# 3. Run, capturing UART0 to a file.
qemu-system-xtensa.exe -nographic -machine esp32s3 -m 8M \
    -global driver=ssi_psram,property=is_octal,value=true \
    -drive file=build/qemu_test.bin,if=mtd,format=raw \
    -serial file:qemu_test.log -monitor null &
QPID=$!
sleep 30                       # let it boot + run
kill $QPID
wait $QPID

# 4. Grep the log.
grep "^I.*bios:" qemu_test.log | head -50
```

### Build flags useful for QEMU testing

| Flag                            | Purpose                                       |
|---------------------------------|-----------------------------------------------|
| `-DESPDOS_LOG_OUT=1`            | Mirror BIOSOUT through `ESP_LOGI` so program output appears in the UART0 log. Without this, programs write only to USB-Serial-JTAG, which QEMU's `-serial file:` does not capture. **Almost always wanted in QEMU.** |
| `-DESPDOS_AUTOPICK=N`           | Pre-feed digit `N` to SHELL after the date prompt. Lets us test SHELL → child program dispatch without an interactive terminal. |
| `-DESPDOS_LOADER_<NAME>=1`      | Boot directly into a single program, skipping SHELL. Useful for isolating which transient broke. |
| `-DESPDOS_HEARTBEAT=1`          | Per-beat instruction-count log line. Off by default; turn on if you suspect the emulator is stuck. |

### What QEMU catches that host tests don't

- ESP-IDF init failures (PSRAM, partition table, bootloader)
- bios.c bugs (auto-feed string scope, control-char filter, JTAG
  buffer overflow under load)
- Build-flag scope bugs (e.g. a `target_compile_definitions(${COMPONENT_LIB})`
  that only reaches one component when two need it)
- The merge_bin step itself (forgetting `0x310000 disk.img` is a
  classic; symptom is "kernel boots but transient runs as garbage")

### What QEMU still doesn't catch

- USB host timing — bios_out's non-blocking `usb_serial_jtag_write_bytes`
  with TX buffer full *only* drops chars on real hardware where the
  USB drains at finite speed. QEMU's JTAG sink consumes infinitely
  fast (or not at all if not redirected).
- ANSI rendering by `idf.py monitor` — QEMU just dumps bytes to the
  log, doesn't simulate a terminal.
- Real keyboard input — there's no live stdin in `-serial file:`
  mode. `-DESPDOS_AUTOPICK=N` works around this for SHELL.
- Boot timing for end-user UX (MEMSCAN takes ~2 s on hardware, less
  in QEMU).

### Killing orphan QEMU processes

Bash & PowerShell don't always clean up the QEMU child cleanly,
especially if the parent shell crashes. Symptom: next QEMU run can't
write the log file ("Device or resource busy") or you see two
serial-flash adapters reported.

```powershell
Get-Process | Where-Object {$_.Name -like "*qemu*"} | Stop-Process -Force
```

This is the first command to run when something looks weird in QEMU.

## Layer 3: Hardware

```powershell
C:\Users\zombo\esp\v5.4.1\esp-idf\export.ps1
cd C:\Users\zombo\desktop\programming\dosNew\esp-dos\firmware
idf.py fullclean
idf.py build -DESPDOS_LOADER_SHELL=1
idf.py flash monitor
```

What only Layer 3 confirms:

- ANSI color and cursor-home work in `idf.py monitor`. (Mostly yes,
  with caveats — see below.)
- Animation feels like animation (frames per second is enough).
- Real keystrokes drive SHELL.
- USB-Serial-JTAG performance under sustained writes (e.g. JULIA's
  ~470-byte rows × 24 rows × 30 frames).

### `idf.py monitor` quirks

- It mostly interprets ANSI escapes (color, cursor-home), but version
  drift across IDF releases changes which ones it strips. If colors
  don't render, try `idf.py monitor --no-color`, or close monitor and
  open the COM port directly in PuTTY/TeraTerm with terminal emulation
  set to `xterm-256color`.
- Exit with `Ctrl-]`. Reset board with `Ctrl-T Ctrl-R` from inside
  monitor.
- COM4 (or wherever the board lives) gets held by monitor; flashing
  fails with "port is busy" if a stale monitor is still running.
  `Get-Process | Where-Object {$_.Name -match "python|idf_monitor"}`
  to find them.

## Recommended workflow when changing emulator code

1. Edit `firmware/components/esp8086/esp8086.c`.
2. `cd tests/emu && mingw32-make run`. Expect 10/10 PASS in <5 s.
3. If a test fails, fix and re-run. Don't proceed until green.
4. Build firmware (Layer 2 setup) and run a relevant QEMU variant.
5. Eyeball the log for surprises. Confirm program outputs match.
6. Only then flash to hardware.

## Recommended workflow when changing a transient (.asm)

1. Edit the `.asm` file.
2. `bash asm/build_kernel.sh` — should produce the new `.bin` files.
3. `py tools/build_disk.py build/disk.img` — disk image rebuilt with
   the new transient.
4. `cd tests/emu && mingw32-make run`. The relevant test (test_mandel,
   test_julia, test_loader) re-loads the disk image and re-runs.
5. If green, build firmware and QEMU-test the variant you changed
   (`-DESPDOS_LOADER_<NAME>=1` or via SHELL with autopick).
6. Flash to hardware once QEMU is happy.

## Recommended workflow when changing bios.c or sdkconfig

Skip Layer 1 (host tests don't compile bios.c). Go straight to
Layer 2.

1. Edit `firmware/components/bios/bios.c` or `sdkconfig.defaults`.
2. **If you changed sdkconfig.defaults**, also delete
   `firmware/sdkconfig` so it regenerates from defaults — `idf.py
   fullclean` does NOT delete sdkconfig.
3. `idf.py fullclean && idf.py build "-DESPDOS_LOADER_SHELL=1"
   "-DESPDOS_AUTOPICK=N" "-DESPDOS_LOG_OUT=1"`.
4. Merge_bin + run QEMU + grep the log.
5. Confirm 10/10 host tests still pass (catches if the change broke a
   shared header somehow).

## Why no PSRAM/peripheral mocks for host tests

Considered and rejected. The mocks would have to track real ESP-IDF
behavior over IDF version drift; the value (catching a class of bug
host tests already mostly cover via the emulator-side path) is small
relative to the maintenance cost. QEMU does the integration check
"for free" — same firmware binary, same kernel, same transients.
