# espDos: dual-target (S3 + C5) with ST7735 status+log mirror

**Status:** draft, awaiting user review
**Date:** 2026-05-07
**Targets:** ESP32-S3 (LilyGO T-Display-S3, current) and ESP32-C5 (LilyGO T-Dongle-C5, new)
**ESP-IDF baseline:** v6.0 for both targets

## Goal

Add ESP32-C5 as a second supported target for espDos, retaining the existing
T-Display-S3 build unchanged. On the C5 dongle, mirror the BIOS console output
to the onboard 0.96" ST7735 LCD (160 × 80 in landscape) as a small status bar
plus a scrolling 80-column log rendered with subpixel anti-aliasing
(Bowman/ClearType-style) so the dongle shows a real DOS-width terminal at a
viewing distance of "USB stick on a desk".

The S3 build keeps using USB-Serial-JTAG for all I/O — no LCD work on that
target. TF card storage on the C5 is explicitly deferred to a follow-up
sub-project.

## Non-goals

- TF / SD card disk backend on the C5 (kept as future sub-project; current
  flash `dos_disk` partition continues to be the file system on both targets).
- Wi-Fi, Bluetooth, BLE, or any networking — espDos has zero network surface
  and gains nothing from the C5's Wi-Fi 6 radio in this round.
- ANSI cursor-positioning fidelity on the LCD (cursor-home, ESC[r;cH, color
  escapes are stripped before logging; programs that redraw in place — JULIA,
  LIFE — appear as append-and-scroll on the LCD while the USB-JTAG side
  receives the full unaltered ANSI stream).
- LCD output on the S3 build. The T-Display-S3's panel is wired but currently
  unused; bringing it up is out of scope here. The architecture leaves room
  for it as a future flip.
- Per-program graphical takeover (MANDEL pixels, JULIA color frames,
  graphical SHELL menu). Real payoff per program but a different product;
  call it a future sub-project that builds on this one.

## Architecture

### Top-level

```
                                +-> usb_serial_jtag (both targets)
bios_out(ch) ---> dispatch -----+
                                +-> display_putc(ch) (C5: real, S3: no-op via Kconfig)
```

Two targets share one source tree. Target picked at configure time
(`idf.py set-target esp32{s3,c5}`). The display is a **subscriber** to BIOS
console output, not a replacement: `bios.c` keeps writing every byte to
USB-Serial-JTAG exactly as it does today; on C5 builds it additionally calls
`display_putc(ch)`, which the new `display` component implements. On S3 builds
the `ESPDOS_HAS_DISPLAY` Kconfig is `n` and the call compiles out, so
`bios.c` stays target-agnostic.

Everything else — kernel blob, 8086tiny core, BIOS dispatch, FAT12 disk
partition, autodate feed, build flags `ESPDOS_LOADER_*`, host test harness,
NASM transients — is unchanged across targets.

### File layout

```
firmware/
  CMakeLists.txt                  unchanged from the v6.0 prep cleanup
  partitions.csv                  unchanged (16 MB layout)
  sdkconfig.defaults              shared keys (flash size, partition table,
                                  optimization, autodate behavior)
  sdkconfig.defaults.esp32s3      S3-specific overlay (octal PSRAM,
                                  ESPDOS_HAS_DISPLAY=n)
  sdkconfig.defaults.esp32c5      C5-specific overlay: CONFIG_SPIRAM_MODE_QUAD=y
                                  (C5 silicon only supports quad PSRAM),
                                  ESPDOS_HAS_DISPLAY=y, ESPDOS_DISPLAY_SUBPIXEL=y
  components/
    bios/                         existing — small change in bios_out
    esp8086/                      unchanged
    kernel_blob/                  unchanged
    display/                      NEW
      Kconfig                     ESPDOS_HAS_DISPLAY,
                                  ESPDOS_DISPLAY_SUBPIXEL,
                                  ST7735 GPIO defaults from LilyGO source:
                                  MOSI=2, SCK=6, CS=10, DC=3, RST=1, BL=0;
                                  SPI freq 40 MHz; all overridable
      CMakeLists.txt              REQUIRES esp_lcd; component is gated on
                                  ESPDOS_HAS_DISPLAY so S3 builds skip it
      include/display.h           public API (4 entry points)
      include/font_6x8.h          sharp 6x8 monospace bitmap (committed)
      include/font_6x8_subpixel.h subpixel-rasterized parallel table (committed)
      display.c                   init, ring buffer, dirty-row tracking,
                                  layout glue
      render_sharp.c              26-col mode (status row + fallback log)
      render_subpixel.c           80-col mode (log rows; only compiled when
                                  ESPDOS_DISPLAY_SUBPIXEL=y)
      st7735.c                    esp_lcd_panel wrapper. Init:
                                  SWRESET, SLPOUT, MADCTL=0xA8 (rotation 3
                                  + BGR bit 0x08 set per panel), INVON
                                  (panel uses inverted display), COLMOD=0x05
                                  (16-bit), DISPON. Address-window writes
                                  apply COLSTART=26 / ROWSTART=1 offsets
                                  (panel is a 132x162 controller windowed
                                  to 80x160). SPI DMA per-row writes.
                                  Two extra GPIO actions outside esp_lcd:
                                  drive PIN_LCD_BL (GPIO 0) high after
                                  init to enable backlight (LilyGO driver
                                  doesn't touch it; LVGL binding does);
                                  drive SD CS (GPIO 23) high once at boot
                                  so the SD slave on the shared SPI bus
                                  stays deselected during LCD transactions.
  main/
    espdos.c                      calls display_set_program/_beat behind
                                  ESPDOS_HAS_DISPLAY guard
tools/
  build_subpixel_font.py          NEW. Offline rasterizer that takes the
                                  sharp 6x8 font, renders each glyph at 3x
                                  horizontal, box-filters into RGB triplets,
                                  emits font_6x8_subpixel.h. Run only when the
                                  base font changes; output is committed.
docs/
  superpowers/specs/2026-05-07-c5-dual-target-design.md   this doc
```

### Display subsystem internals

Three layers inside the new component:

```
+--------------------------------------------------------------+
|  display_putc(ch)            display_set_program / _beat     |
|     |                              |                         |
|     v                              v                         |
|  ring buffer       +---------+  status struct                |
|  (10 lines x       |  layout |  (program name, beat,         |
|   80 chars)        |  glue   |   8086 freq estimate)         |
|                    +----+----+                               |
|                         |                                    |
|                         v                                    |
|             +-----------------------+                        |
|             |   row renderer        |                        |
|             |   - sharp 26-col       (status row)            |
|             |   - subpixel 80-col    (log rows; #ifdef)      |
|             +-----------+-----------+                        |
|                         |                                    |
|                         v   one row of RGB565 (160 px x 8 px)|
|             +-----------+-----------+                        |
|             |  esp_lcd_panel_st7735 |                        |
|             |  SPI DMA, MADCTL set  |                        |
|             |  for landscape + RGB  |                        |
|             +-----------------------+                        |
+--------------------------------------------------------------+
```

#### Public API

```c
esp_err_t display_init(void);
void      display_putc(uint8_t ch);
void      display_set_program(const char *name);
void      display_set_beat(uint32_t beat);
```

These are the only symbols `bios.c` and `espdos.c` see. On S3 they're
provided as inline no-ops in `display.h` behind `#if !CONFIG_ESPDOS_HAS_DISPLAY`,
so the call sites need no `#ifdef` of their own.

#### `display_putc`: ANSI strip

The log shows the textual content of the BIOS output stream with control
sequences swallowed. State machine:
- Outside an escape: append printable bytes to the active log row;
  on `\n`, advance to next row (scroll if full); on `\r`, reset column.
- Saw `ESC` (0x1B): read the next byte. If it's `[`, we're in a CSI
  sequence — keep swallowing until a final byte in `0x40..0x7E`
  arrives. If it's any other byte, that byte itself is the terminator
  (covers `ESC c`, `ESC D`, etc.). This avoids the trap of treating
  the `[` itself as a CSI terminator.

Cursor-home programs (JULIA's `ESC[H` per frame, LIFE's redraws) end up
doing pure append-and-scroll on the LCD — correct behavior for a log
window. The USB-Serial-JTAG side receives the unmodified stream, so a
real terminal still gets the in-place animations.

Color in the log is deferred. `display_putc` doesn't track the active
ANSI color. Each rendered glyph is white-on-black initially. A future
extension can stash a 4-bit color per char in the ring buffer if we want
the log to track JULIA's per-pixel hues.

#### Render cadence

A 30 Hz FreeRTOS timer flushes only dirty rows. `display_putc` marks the
active log row dirty; `display_set_program/_beat` mark the status row
dirty. No per-character render. SPI DMA writes one dirty 160 × 8 row per
flush in roughly 5 ms (25.6 KB framebuffer at ~5 MB/s).

#### Subpixel rasterizer

Cell math: in landscape, the panel is 160 RGB pixels wide × 80 rows tall.
Each RGB pixel contributes three horizontal subpixel slots (one each
for R, G, B), so the logical horizontal resolution for text purposes
is 480 subpixels. Vertically the panel is unchanged at 80 rows — there
is no vertical subpixel structure on a stripe LCD. A character cell of
6 subpixels wide × 8 rows tall therefore tiles to 80 columns × 10 rows.

Per-glyph rasterization:
1. Look up the 6 × 8 bitmap glyph (24 bits per row × 8 rows after 3x
   horizontal upsample).
2. For each row, group the 18 horizontal samples into 6 subpixel
   triplets. Each triplet maps to one packed pixel. **The panel is
   BGR-ordered** (confirmed in `lib/lcd_st7735/st7735.h`: MADCTL bit
   `0x08` is always set), so triplet position 0 → B intensity, position
   1 → G, position 2 → R. The script knows this and emits BGR-mapped
   RGB565 words. The initial filter is a 3-tap `[1, 2, 1]` weighted
   average centered on each subpixel slot (gentle anti-color-fringing,
   matches ClearType-style rasterizers). If the result over-blurs glyph
   edges in practice, the script can switch to a tighter `[0, 1, 0]`
   (no filtering, max fringe, sharpest glyphs) without on-chip code
   changes — the committed table is what changes.
3. Emit 6 RGB565 words to the row framebuffer.

The committed table is `font_6x8_subpixel.h`, ~12 KB
(`uint16_t [256][8][6]`). On-chip path is a pure lookup — no runtime
filtering, no per-glyph math.

The status bar uses `render_sharp.c`'s plain 6 × 8 path — no fringes on
the always-visible chrome. The log uses `render_subpixel.c` when
`ESPDOS_DISPLAY_SUBPIXEL=y`, falling back to `render_sharp.c`'s 26-col
wrap-and-truncate path when it's `n`.

#### Offline font generator

`tools/build_subpixel_font.py`:
- Input: the Spleen 6 × 8 monospace bitmap font (public domain; used
  for both the sharp status row and as the source bitmap that the
  subpixel rasterizer upsamples). Committed as `font_6x8.h` directly
  so neither build nor font script depends on a TTF rasterizer.
- Output: `components/display/include/font_6x8_subpixel.h` — a static
  `const uint16_t font_subpixel[256][8][6]` table where each entry is
  one row's 6 RGB565 pixels for that glyph.
- Algorithm: render the 6-pixel-wide glyph at 18 pixels wide via
  nearest-neighbor upsample, box-filter each 3-pixel group into one
  R/G/B intensity (channel = average of the 3 source pixels assigned
  to that channel), pack into RGB565.

The script is run by hand when the base font changes. The build does
not depend on Python at compile time — both `.h` files are committed to
the repo. A golden-output check inside the script (renders "Hello,
86-DOS!" and diffs against a committed PNG) catches regressions when
the font source is edited.

### Build & flash invocation

```
# S3 (existing experience, behavior unchanged)
idf.py set-target esp32s3
idf.py build flash monitor

# C5 (new)
idf.py set-target esp32c5
idf.py build flash monitor
```

`idf.py set-target` automatically picks up the matching
`sdkconfig.defaults.<target>` overlay alongside the shared
`sdkconfig.defaults`. Existing `-DESPDOS_LOADER_*` flags work on both
targets unchanged.

The README gains a short "C5 target" subsection alongside the existing
S3 instructions, documenting the `set-target` step and the LCD behavior.

## Confirmed hardware details (from LilyGO source)

These were open questions in earlier drafts and are now resolved by
reading `examples/Factory/pin_config.h`, `lib/lcd_st7735/st7735.h`, and
`lib/lcd_st7735/st7735.cpp` in the LilyGO T-Dongle-C5 repo.

| Item | Value | Source |
|---|---|---|
| LCD MOSI | GPIO 2 | `pin_config.h` |
| LCD SCK | GPIO 6 | `pin_config.h` |
| LCD CS | GPIO 10 | `pin_config.h` |
| LCD DC | GPIO 3 | `pin_config.h` |
| LCD RST | GPIO 1 | `pin_config.h` |
| LCD BL (backlight) | GPIO 0 | `pin_config.h` |
| LCD SPI clock | 40 MHz | `st7735.cpp` `SPISettings(40000000, ...)` |
| Panel resolution | 80 × 160 native, 160 × 80 in landscape (rotation 3) | `st7735.h`, `lcd.ino` |
| Panel color order | **BGR** (MADCTL bit `0x08` always set) | `st7735.cpp` `_madctl = (rotation_config[m] & 0xF7) \| 0x08` |
| Panel display mode | inverted (`INVON` in init) | `st7735.h` "Inverted" |
| Address window offsets | COLSTART = 26, ROWSTART = 1 | `st7735.h` private members |
| C5 PSRAM | quad SPI (C5 silicon does not support octal) | Espressif ESP32-C5 datasheet |
| C5 USB | native USB-Serial-JTAG, GPIO 13/14 | C5 silicon spec; `bios.c` works as-is |
| TF/SD card SPI | shares LCD's SPI bus, CS = GPIO 23 | `pin_config.h` (out of scope this round; relevant for the future TF sub-project) |

## Open items remaining

1. **C5 QEMU support.** Espressif's QEMU fork has RISC-V support but
   coverage of `esp32c5` specifically is recent. Verified at
   implementation time via `qemu-system-riscv32 --machine help`. If
   absent, C5 testing falls back to Layer 1 (host) + Layer 3 (hardware)
   only; S3 Layer 2 path is unaffected.

## Testing

The project's existing testing model is documented in `docs/testing.md`
and runs in three layers, fast → slow:

| Layer | What it runs | Iteration | What it catches |
|---|---|---:|---|
| 1: host tests | `tests/emu/test_*.c` — host gcc compiles `esp8086.c` directly | ~1 s | emulator + kernel + transient logic |
| 2: QEMU | `qemu-system-xtensa.exe` (esp-develop-9.2.2-20260417) running merged flash image | ~5 s | ESP-IDF init, PSRAM, partitions, `bios.c` paths via `ESPDOS_LOG_OUT=1` |
| 3: hardware | `idf.py flash monitor` on the actual device | ~90 s | USB timing, monitor ANSI rendering, real input |

This work uses all three layers but each one needs a small adjustment.

### Layer 1: host tests

Existing 10 host tests stay green throughout — they target `esp8086.c`
and the kernel, both of which are unchanged.

**Add three new host tests** to cover the display logic that *is* host-
testable (the parts that don't depend on `esp_lcd_panel_*` or FreeRTOS):

| New test | What it asserts |
|---|---|
| `test_display_ansi_strip` | Drive a stream containing CSI sequences, OSC strings, and bare `ESC c` through the strip state machine; assert the log buffer contains exactly the printable text. The state-machine bug from the self-review (treating `[` as a CSI terminator) gets a regression case. |
| `test_display_ring_buffer` | Append 30 lines to a 10-line ring; assert oldest 20 are gone, newest 10 are in order, dirty-row marks correctly identify the wrapped slot. |
| `test_subpixel_glyph_table` | Cross-check `font_6x8_subpixel.h` against `font_6x8.h`: re-run the same `[1, 2, 1]` filter inline in C, assert table matches. Catches the case where someone hand-edits the subpixel header without re-running the font script. |

These tests link against the same display source files the firmware
uses, with `esp_lcd_panel.h` etc. excluded behind a `#ifdef
ESP_PLATFORM` guard the way `esp8086.c` already does. The pure-C parts
(state machine, ring buffer, glyph table) are the only thing the tests
exercise; SPI/DMA stays out.

### Layer 2: QEMU

**S3 target — unchanged.** The QEMU recipe in `docs/testing.md` (Xtensa
QEMU, octal PSRAM, merged 8 MB flash, `ESPDOS_LOG_OUT=1`) continues to
be the regression check before any S3 flash. Display Kconfig is `n` on
S3 builds, so display code compiles out — QEMU's job is the same
existing-behavior verification as today.

**C5 target — TBD, best-effort.** The Espressif QEMU fork supports
RISC-V chips (`qemu-system-riscv32`), but C5 support specifically is
recent and not yet confirmed in our installed binary. Concrete plan:
during implementation, run `qemu-system-riscv32 --machine help` against
the installed binary to check for `esp32c5`. If supported, write a C5
QEMU recipe parallel to the S3 one (different machine, different PSRAM
flag — C5 is quad SPI, not octal). If not supported, document the gap
and use Layer 1 + Layer 3 only on C5 for now. This is an open question
that resolves at implementation time, not a design blocker.

### Layer 3: hardware verification gates

Manual, in this order. Each gate is "if this fails, stop and triage
before proceeding" so failures can't compound. Per the existing
workflow, every gate is preceded by Layer 1 green and (for S3) Layer 2
green.

1. **S3 regression.** Flash T-Display-S3 with the new shared sdkconfig
   structure. SHELL menu boots, MANDEL renders, USB-JTAG console works
   as today. Proves the dual-target plumbing didn't break the existing
   platform.

2. **C5 minimal boot.** Flash T-Dongle-C5 with display disabled
   (`ESPDOS_HAS_DISPLAY=n` overlay variant). MANDEL boots, output
   appears over USB-Serial-JTAG. Proves emulator + flash partition +
   console all work on C5 silicon and IDF 6.0 before display enters
   the picture.

3. **C5 with display, sharp mode only.** Enable `ESPDOS_HAS_DISPLAY=y`
   but leave `ESPDOS_DISPLAY_SUBPIXEL=n`. Status bar and log both
   render in 26-col sharp mode. Confirms ST7735 init, MADCTL landscape,
   SPI DMA, ring buffer, dirty-row flush all work end-to-end before
   adding subpixel.

4. **Subpixel calibration.** Render the three-color subpixel test
   pattern from open question 3. If stripe order is wrong, flip
   MADCTL bit 3 and recommit the C5 sdkconfig.

5. **Full C5 experience.** Enable both Kconfigs. Run JULIA: USB-JTAG
   side shows full color animation, LCD log shows scrolling text in
   80-col subpixel mode, status bar shows `JULIA.COM | beat NNNN`.
   Run LIFE, then SHELL, confirm menu navigation looks right.

### Workflow per file edited

Following the patterns in `docs/testing.md`:

| Files touched | Layer 1 | Layer 2 | Layer 3 |
|---|:---:|:---:|:---:|
| `components/display/*` | new tests above | S3: regression only; C5: if supported | both targets |
| `components/bios/bios.c` (the new `display_putc` call) | skip (host doesn't compile bios.c) | required on S3 | both targets |
| `sdkconfig.defaults*` | skip | required on S3 (delete `firmware/sdkconfig` first so defaults regenerate) | target whose sdkconfig changed |
| `tools/build_subpixel_font.py` + regenerated `font_6x8_subpixel.h` | `test_subpixel_glyph_table` re-asserts table | not affected | not affected |
| Anything in `components/esp8086/` or kernel/transient asm | full existing 10-test suite | both targets | both targets |

## Decisions deferred

- **Color in the log.** Subpixel rasterizer ships monochrome white-on-
  black. Per-glyph color tracking from ANSI escapes is straightforward
  follow-up but not blocking.
- **S3 LCD.** The T-Display-S3 has a panel that espDos currently doesn't
  use. Wiring it up via this same `display` component is feasible — the
  component is target-conditional but not target-coupled — and is a
  natural follow-up sub-project.
- **TF card disk.** Replacing the 384 KB flash `dos_disk` partition with
  a multi-MB FAT image on the C5's TF card is its own sub-project; it
  touches `bios.c` disk paths and FAT12 image tooling more deeply than
  the LCD work and is sized accordingly.
