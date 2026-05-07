# C5 dual-target — progress + handoff (2026-05-07)

Snapshot of the `c5-dual-target` branch state at the end of the
working session. Pick this up to finish Hardware Gates 4+5 and
ship the work.

**Spec:** `docs/superpowers/specs/2026-05-07-c5-dual-target-design.md`
**Plan:** `docs/superpowers/plans/2026-05-07-c5-dual-target.md`

## What works

End-to-end on hardware (T-Dongle-C5):

- C5 boots, kernel runs, MANDEL/SHELL render over USB-Serial-JTAG
  identically to the S3 path. (Hardware Gate 2 ✅)
- LCD lights up. Status bar at the top of the panel shows
  `<PROGRAM>.COM` + `bNNNNNNN` heartbeat counter, sharp 26-col
  white-on-black, updated at ~30 Hz. (Hardware Gate 3 ✅)
- Log area below the status bar scrolls program output. With
  `CONFIG_ESPDOS_DISPLAY_SUBPIXEL=n` (sharp 26-col fallback), text
  is verified rendering MANDEL ASCII end-to-end through the full
  pipeline (`bios_out` → `display_putc` → ANSI strip → ring buffer
  → 30 Hz timer flush → render_sharp_scanline → SPI DMA to ST7735).
- S3 build + boot still works unchanged after the dual-target
  refactor. (Hardware Gate 1 ✅)

Off hardware:

- 13/13 host tests PASS (10 emulator + 3 display: ANSI strip, ring
  buffer rotation, subpixel glyph table cross-check).
- Both `idf.py set-target esp32s3` and `idf.py set-target esp32c5`
  builds clean from a wiped `build/`.

## What's pending

Hardware Gates 4 + 5 — both require the T-Dongle-C5, the firmware
already built with `CONFIG_ESPDOS_DISPLAY_SUBPIXEL=y` (the default
for the C5 overlay).

**Gate 4:** Subpixel calibration. The `tools/build_subpixel_font.py`
output assumes BGR stripe order (per LilyGO's MADCTL bit `0x08`
always set). If the on-panel result shows color fringes in the
*wrong* order (e.g. visibly blue on the right side of strokes
instead of the left), flip the channel mapping in
`render_glyph()` (swap `cell*3 + 0` ↔ `cell*3 + 2`), regenerate
the header (`python tools/build_subpixel_font.py`), and reflash.
Optional: enable `CONFIG_ESPDOS_DISPLAY_CALIBRATION` for an
explicit boot-time three-color stripe test pattern.

**Gate 5:** Full C5 experience. Flash with the C5 default build,
verify SHELL menu renders in 80-col subpixel mode below the status
bar, pick `4` (JULIA), verify the LCD log scrolls Julia's
animation output (control chars stripped, color fringes from
subpixel rendering) while USB-Serial-JTAG shows the full color
animation. Run LIFE too. Confirm USB-JTAG side has zero
regressions.

After both gates pass, the work is ready to merge to `main`.

## How to resume on a fresh machine

1. Clone with submodules:
   ```bash
   git clone --recurse-submodules https://github.com/zombodotcom/espDos.git
   cd espDos
   git checkout c5-dual-target
   ```

2. Install the prereqs:
   - **ESP-IDF v6.0.1** via the IDF Installation Manager (default
     install path on Windows: `C:\esp\v6.0.1\esp-idf\`). v5.4.x
     does not work — see "ESP-IDF 6.0 quirks" below for why.
   - **NASM** (`scoop install nasm` on Windows).
   - **Python 3** via Scoop or the Microsoft Store (use `python`,
     not `py`).
   - **MinGW-w64 GCC** for the host tests (`scoop install gcc`).

3. Build the kernel + transient + disk image (one-shot, target-
   agnostic):
   ```bash
   bash asm/build_kernel.sh
   python tools/build_disk.py build/disk.img
   ```

4. Build the firmware for whichever device is plugged in:
   ```powershell
   & 'C:\esp\v6.0.1\esp-idf\export.ps1'
   cd firmware
   idf.py set-target esp32c5      # or esp32s3
   idf.py fullclean
   idf.py build flash monitor
   ```

## Architecture summary (what we built)

- **Single source tree, target picked at configure time.** Shared
  `firmware/sdkconfig.defaults` plus per-target overlays
  (`sdkconfig.defaults.esp32{s3,c5}`) cover PSRAM mode, display
  enablement, and target-specific Kconfig.
- **Display is a subscriber to `bios_out`, not a coupled
  dependency.** `bios.c` writes every byte to USB-Serial-JTAG
  unconditionally and *additionally* calls `display_putc(ch)`,
  which is an inline no-op on S3 (`CONFIG_ESPDOS_HAS_DISPLAY=n`)
  and the real implementation on C5.
- **`firmware/components/display/`** owns the LCD pipeline:
  - `display_log.c` — pure C ANSI strip + 9-row ring buffer
    (host-tested in `tests/emu/test_display_*.c`).
  - `render_sharp.c` — 26-col 6×8 white-on-black, used for the
    status bar always and the log when subpixel is off.
  - `render_subpixel.c` — 80-col Bowman/ClearType-style subpixel
    rendering, table-lookup against the offline-generated
    `font_6x8_subpixel.h`. Used for the log when
    `CONFIG_ESPDOS_DISPLAY_SUBPIXEL=y` (default on C5).
  - `st7735_panel.c` — direct port of LilyGO's Adafruit_ST7735
    driver (lib/lcd_st7735/st7735.{h,cpp}, MIT-derived). Hardware
    reset, init command stream verbatim, MADCTL=0xA8 (rotation 3
    + BGR), backlight active LOW, SD CS deselected.
  - `display.c` — top-level glue: `display_init()`, FreeRTOS
    xTimer at ~30 Hz, dirty-row tracking and SPI DMA flush.
- **Subpixel font generator** at `tools/build_subpixel_font.py`
  (offline, run once when the source font changes; output is
  committed in `firmware/components/display/include/`).

## Things that bit us along the way

Worth knowing for the next person debugging this:

1. **ESP-IDF 6.0 strict-aliasing default.** v6.0 makes warnings
   errors by default. The 8086tiny `CAST` macro does
   memcpy-style type-punning that's intentional but trips
   `-Werror=strict-aliasing`. Fix: per-component
   `-Wno-strict-aliasing` (commit `9fe168e`).

2. **C5 SOC capability Kconfig propagation.** ESP-IDF 6.0.1's
   root `Kconfig` `orsource` for
   `components/soc/$IDF_TARGET/include/soc/Kconfig.soc_caps.in`
   silently drops ~133 of 332 SOC_* capabilities on C5,
   including `SOC_USB_SERIAL_JTAG_SUPPORTED`. That cap gates
   `esp_driver_usb_serial_jtag`'s source list; without it,
   `bios.c` fails to link. Workaround:
   `firmware/Kconfig.projbuild` redeclares the cap. Remove if
   IDF fixes upstream.

3. **Waveshare ST7735 driver bugs vs LilyGO/Adafruit init.**
   Espressif Components Registry's `waveshare/esp_lcd_st7735`
   v1.0.1:
   - sends NORON/DISPON with a stray `{0x00}` data byte (those
     commands take zero arguments per the ST7735 datasheet);
   - sends INVOFF in init when the GREENTAB160x80 panel needs
     INVON in-stream;
   - hardware reset is 10 ms / 10 ms vs Adafruit's 100/100/120ms
     (this panel needs the longer pulse).

   Net effect: panel stayed dark with backlight on. Replaced
   with a direct C port of LilyGO's `lib/lcd_st7735/st7735.cpp`
   which is itself derived from Adafruit_ST7735.

4. **Backlight is ACTIVE LOW.** LilyGO's example does
   `digitalWrite(PIN_LCD_BL, 0)` to turn the backlight ON. We
   were driving the pin HIGH, leaving the panel dark.

5. **`firmware/sdkconfig` is tracked but volatile.** Every
   `idf.py set-target` regenerates it. The `.defaults` files are
   the source of truth; the live `sdkconfig` is best treated as
   build state. (Not gitignored yet — see open question below.)

## Open questions to resolve before merge

- **Track `firmware/sdkconfig` or gitignore it?** It currently
  flips on every set-target between targets. The two
  `sdkconfig.defaults*` files are the actual source of truth.
  Adding `firmware/sdkconfig` to `.gitignore` would stop the
  churn but breaks reproducibility for anyone who relied on the
  committed snapshot. Project author's call.
- **Subpixel BGR/RGB confirmation.** Confirm visually at Gate 4
  that the channel mapping is correct (or flip in
  `tools/build_subpixel_font.py` and regen).
- **C5 QEMU recipe.** The spec lists this as TBD. Test
  `qemu-system-riscv32 --machine help` for `esp32c5`; if
  available, write a parallel recipe to `docs/testing.md`'s S3
  one. If absent, document the gap.

## Commits on this branch (since `main`)

```
21df6d1 display: real subpixel 80-col renderer (replaces all-black stub)
9013f37 espdos: default to SHELL.COM instead of MANDEL.COM
21c3c63 display: replace waveshare ST7735 + esp_lcd with LilyGO Adafruit port
85eeefe display: wire init + 30Hz flush + sharp-mode render
12b7e63 display: sharp 26-col renderer
1c76a4e display: ST7735 panel driver (SPI, MADCTL, offsets, BL, SD CS, vendored ST7735)
c9ed9f1 display: subpixel glyph table cross-check host test
29f82fd display: ring-buffer rotation host test
e9197a8 display: ANSI strip state machine + host test
2bd63cb display: subpixel font generator + committed font_6x8_subpixel.h
b3f4d77 display: add Spleen 6x8 bitmap font
25fd5a2 display: scaffold component with no-op API; hook bios + main
2b91a9a firmware: add ESP32-C5 sdkconfig overlay; build target available on T-Dongle-C5
9fe168e esp8086: silence strict-aliasing under ESP-IDF 6.0 GCC
1e203b7 firmware: split sdkconfig.defaults into shared + S3 overlay
07788c0 build: add Paterson-Listings as submodule; fall-back path in build_kernel.sh
9cff5a9 firmware: bump cmake to 3.22 for ESP-IDF 6.0; drop v5.4 GCC ICE workaround
```

Plus the design + plan docs already on `main`:

```
8db0788 docs: implementation plan for ESP32-C5 dual-target
28ca287 docs: design spec for ESP32-C5 dual-target with ST7735 status+log mirror
```
