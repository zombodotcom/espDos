#!/bin/sh
# Build the 86-DOS 1.00 kernel from Tim Paterson's source.
# Tries kernel.patches.asm first (NASM-syntax wrapper); falls back to
# the raw source if no patches file exists.

set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# Prefer the in-repo submodule (espDos/Paterson-Listings/...). Fall
# back to a sibling clone (../Paterson-Listings/...) for backwards
# compat with pre-submodule layouts.
if [ -f "$ROOT/Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM" ]; then
    SRC="$ROOT/Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"
else
    SRC="$ROOT/../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"
fi
OUT_DIR="$ROOT/build"
mkdir -p "$OUT_DIR"

[ -f "$SRC" ] || {
    echo "Source not found at either:" >&2
    echo "  $ROOT/Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM" >&2
    echo "  $ROOT/../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM" >&2
    echo "Run: git submodule update --init Paterson-Listings" >&2
    exit 1
}

# Find a working python interpreter. On Windows the `python` command
# is often a Microsoft Store alias that prints a help banner instead
# of running, so we actually invoke each candidate with --version and
# pick the first that succeeds.
PYTHON=""
for cand in py python3 python; do
    if command -v "$cand" >/dev/null 2>&1 \
       && "$cand" --version >/dev/null 2>&1; then
        PYTHON="$cand"
        break
    fi
done
[ -n "$PYTHON" ] || {
    echo "No working python interpreter found (tried py, python3, python)" >&2
    exit 1
}

# Translate SCP-ASM dialect to NASM via the preprocessor. The
# original $SRC is never modified.
TRANSLATED="$OUT_DIR/kernel.translated.asm"
"$PYTHON" "$SCRIPT_DIR/scp_to_nasm.py" "$SRC" > "$TRANSLATED"

nasm -f bin -o "$OUT_DIR/kernel.bin" -l "$OUT_DIR/kernel.lst" "$TRANSLATED"
echo "Built: $OUT_DIR/kernel.bin ($(wc -c < "$OUT_DIR/kernel.bin") bytes)"

# Boot stub — assembled with KERNEL_SEG / KERNEL_OFFSET pulled from
# esp8086.h so there's exactly one source of truth for those addresses.
HDR="$ROOT/firmware/components/esp8086/include/esp8086.h"
[ -f "$HDR" ] || { echo "Missing $HDR" >&2; exit 1; }

# Extract `#define EMU_KERNEL_SEG 0x0100u` -> 0x0100. The header uses
# the `0xNNNu` C-style suffix; strip it for NASM.
extract_define() {
    name="$1"
    grep -E "^#define[[:space:]]+$name[[:space:]]" "$HDR" \
        | head -n1 \
        | sed -E 's/.*'"$name"'[[:space:]]+(0x[0-9A-Fa-f]+)u?.*/\1/'
}
KSEG=$(extract_define EMU_KERNEL_SEG)
KOFF=$(extract_define EMU_KERNEL_OFFSET)
[ -n "$KSEG" ] && [ -n "$KOFF" ] || {
    echo "Could not extract EMU_KERNEL_SEG/OFFSET from $HDR" >&2; exit 1;
}

# Loader — assembled first because bootstub INCBINs build/loader.bin
# at LOADER_OFFSET. Loader's `org` must match the same LOADER_OFFSET
# so its internal label references resolve correctly.
LOADER_OFF=0x100
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -o "$OUT_DIR/loader.bin" \
     -l "$OUT_DIR/loader.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader.bin ($(wc -c < "$OUT_DIR/loader.bin") bytes) " \
     "[LOADER_OFFSET=$LOADER_OFF]"

nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -o "$OUT_DIR/bootstub.bin" \
     -l "$OUT_DIR/bootstub.lst" \
     "$SCRIPT_DIR/bootstub.asm"
echo "Built: $OUT_DIR/bootstub.bin ($(wc -c < "$OUT_DIR/bootstub.bin") bytes) " \
     "[KERNEL_SEG=$KSEG KERNEL_OFFSET=$KOFF LOADER_OFFSET=$LOADER_OFF]"

# Transient HELLO.COM — placed onto disk.img by tools/build_disk.py.
nasm -f bin \
     -o "$OUT_DIR/hellotr.bin" \
     -l "$OUT_DIR/hellotr.lst" \
     "$SCRIPT_DIR/hellotr.asm"
echo "Built: $OUT_DIR/hellotr.bin ($(wc -c < "$OUT_DIR/hellotr.bin") bytes)"

# Transient MANDEL.COM — Q4.12 ASCII Mandelbrot, second file on disk.
nasm -f bin \
     -o "$OUT_DIR/mandel.bin" \
     -l "$OUT_DIR/mandel.lst" \
     "$SCRIPT_DIR/mandel.asm"
echo "Built: $OUT_DIR/mandel.bin ($(wc -c < "$OUT_DIR/mandel.bin") bytes)"

# Variant loader that pulls MANDEL.COM (cluster 3 = sector 12) off the
# disk image instead of HELLO.COM (cluster 2 = sector 11). Same code,
# different sector constant. Built into a parallel bootstub variant
# (bootstub_mandel.bin) so the existing tests keep using the unchanged
# bootstub.bin / loader.bin pair.
MANDEL_SECTOR=12
MANDEL_COUNT=1
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -D"LOAD_SECTOR=$MANDEL_SECTOR" \
     -D"LOAD_COUNT=$MANDEL_COUNT" \
     -o "$OUT_DIR/loader_mandel.bin" \
     -l "$OUT_DIR/loader_mandel.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader_mandel.bin ($(wc -c < "$OUT_DIR/loader_mandel.bin") bytes) " \
     "[LOAD_SECTOR=$MANDEL_SECTOR LOAD_COUNT=$MANDEL_COUNT]"

# bootstub.asm INCBINs build/loader.bin literally; for the mandel
# variant we temporarily swap that file. We assemble into a second
# output and restore. Simpler approach: use a sed-stamp on a copy.
cp "$SCRIPT_DIR/bootstub.asm" "$OUT_DIR/bootstub_mandel.asm.tmp"
sed -i 's|build/loader.bin|build/loader_mandel.bin|' "$OUT_DIR/bootstub_mandel.asm.tmp"
nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -i "$ROOT/" \
     -o "$OUT_DIR/bootstub_mandel.bin" \
     -l "$OUT_DIR/bootstub_mandel.lst" \
     "$OUT_DIR/bootstub_mandel.asm.tmp"
rm -f "$OUT_DIR/bootstub_mandel.asm.tmp"
echo "Built: $OUT_DIR/bootstub_mandel.bin ($(wc -c < "$OUT_DIR/bootstub_mandel.bin") bytes)"

# Transient COUNT.COM — counts 1..50 in decimal. Third file on disk
# (cluster 4 = sector 13). Demonstrates a third independent program
# running through the same kernel + loader path.
nasm -f bin \
     -o "$OUT_DIR/count.bin" \
     -l "$OUT_DIR/count.lst" \
     "$SCRIPT_DIR/count.asm"
echo "Built: $OUT_DIR/count.bin ($(wc -c < "$OUT_DIR/count.bin") bytes)"

COUNT_SECTOR=13
COUNT_COUNT=1
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -D"LOAD_SECTOR=$COUNT_SECTOR" \
     -D"LOAD_COUNT=$COUNT_COUNT" \
     -o "$OUT_DIR/loader_count.bin" \
     -l "$OUT_DIR/loader_count.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader_count.bin ($(wc -c < "$OUT_DIR/loader_count.bin") bytes) " \
     "[LOAD_SECTOR=$COUNT_SECTOR LOAD_COUNT=$COUNT_COUNT]"

cp "$SCRIPT_DIR/bootstub.asm" "$OUT_DIR/bootstub_count.asm.tmp"
sed -i 's|build/loader.bin|build/loader_count.bin|' "$OUT_DIR/bootstub_count.asm.tmp"
nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -i "$ROOT/" \
     -o "$OUT_DIR/bootstub_count.bin" \
     -l "$OUT_DIR/bootstub_count.lst" \
     "$OUT_DIR/bootstub_count.asm.tmp"
rm -f "$OUT_DIR/bootstub_count.asm.tmp"
echo "Built: $OUT_DIR/bootstub_count.bin ($(wc -c < "$OUT_DIR/bootstub_count.bin") bytes)"

# Transient JULIA.COM — animated 16-color Julia set. Cluster 9
# (sectors 18..20 — 1082 bytes spans 3 sectors). Same Q4.12 IMUL
# kernel as MANDEL.COM but with z0=pixel and c=animation parameter,
# plus ANSI 16-color output.
nasm -f bin \
     -o "$OUT_DIR/julia.bin" \
     -l "$OUT_DIR/julia.lst" \
     "$SCRIPT_DIR/julia.asm"
echo "Built: $OUT_DIR/julia.bin ($(wc -c < "$OUT_DIR/julia.bin") bytes)"

JULIA_SECTOR=18
JULIA_COUNT=3
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -D"LOAD_SECTOR=$JULIA_SECTOR" \
     -D"LOAD_COUNT=$JULIA_COUNT" \
     -o "$OUT_DIR/loader_julia.bin" \
     -l "$OUT_DIR/loader_julia.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader_julia.bin ($(wc -c < "$OUT_DIR/loader_julia.bin") bytes) " \
     "[LOAD_SECTOR=$JULIA_SECTOR LOAD_COUNT=$JULIA_COUNT]"

cp "$SCRIPT_DIR/bootstub.asm" "$OUT_DIR/bootstub_julia.asm.tmp"
sed -i 's|build/loader.bin|build/loader_julia.bin|' "$OUT_DIR/bootstub_julia.asm.tmp"
nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -i "$ROOT/" \
     -o "$OUT_DIR/bootstub_julia.bin" \
     -l "$OUT_DIR/bootstub_julia.lst" \
     "$OUT_DIR/bootstub_julia.asm.tmp"
rm -f "$OUT_DIR/bootstub_julia.asm.tmp"
echo "Built: $OUT_DIR/bootstub_julia.bin ($(wc -c < "$OUT_DIR/bootstub_julia.bin") bytes)"

# Transient LIFE.COM — Conway's Game of Life with color cycling.
# Cluster 12 (sectors 21..22 — ~1KB so 2 sectors). Same toroidal
# 78x24 grid as the other ASCII transients. Generations animate via
# ESC[H frame separator.
nasm -f bin \
     -o "$OUT_DIR/life.bin" \
     -l "$OUT_DIR/life.lst" \
     "$SCRIPT_DIR/life.asm"
echo "Built: $OUT_DIR/life.bin ($(wc -c < "$OUT_DIR/life.bin") bytes)"

LIFE_SECTOR=21
LIFE_COUNT=2
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -D"LOAD_SECTOR=$LIFE_SECTOR" \
     -D"LOAD_COUNT=$LIFE_COUNT" \
     -o "$OUT_DIR/loader_life.bin" \
     -l "$OUT_DIR/loader_life.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader_life.bin ($(wc -c < "$OUT_DIR/loader_life.bin") bytes) " \
     "[LOAD_SECTOR=$LIFE_SECTOR LOAD_COUNT=$LIFE_COUNT]"

cp "$SCRIPT_DIR/bootstub.asm" "$OUT_DIR/bootstub_life.asm.tmp"
sed -i 's|build/loader.bin|build/loader_life.bin|' "$OUT_DIR/bootstub_life.asm.tmp"
nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -i "$ROOT/" \
     -o "$OUT_DIR/bootstub_life.bin" \
     -l "$OUT_DIR/bootstub_life.lst" \
     "$OUT_DIR/bootstub_life.asm.tmp"
rm -f "$OUT_DIR/bootstub_life.asm.tmp"
echo "Built: $OUT_DIR/bootstub_life.bin ($(wc -c < "$OUT_DIR/bootstub_life.bin") bytes)"

# Transient SHELL.COM — typed-prompt command processor. Cluster 5
# (sectors 14..17, 4 sectors reserved). bootstub_shell.bin loads it;
# it prints "A>" and accepts typed program names + Enter (and built-ins
# like EXIT/CLS/VER/DATE/TIME/DIR/TYPE).
nasm -f bin \
     -o "$OUT_DIR/shell.bin" \
     -l "$OUT_DIR/shell.lst" \
     "$SCRIPT_DIR/shell.asm"
echo "Built: $OUT_DIR/shell.bin ($(wc -c < "$OUT_DIR/shell.bin") bytes)"

SHELL_SECTOR=14
SHELL_COUNT=4                         # SHELL grows through Phase B/C/D
                                      # built-ins; reserve 4 sectors
                                      # (~2KB cap) so we don't have to
                                      # slide JULIA/LIFE again every
                                      # time it crosses 512.
nasm -f bin \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -D"LOAD_SECTOR=$SHELL_SECTOR" \
     -D"LOAD_COUNT=$SHELL_COUNT" \
     -o "$OUT_DIR/loader_shell.bin" \
     -l "$OUT_DIR/loader_shell.lst" \
     "$SCRIPT_DIR/loader.asm"
echo "Built: $OUT_DIR/loader_shell.bin ($(wc -c < "$OUT_DIR/loader_shell.bin") bytes) " \
     "[LOAD_SECTOR=$SHELL_SECTOR LOAD_COUNT=$SHELL_COUNT]"

cp "$SCRIPT_DIR/bootstub.asm" "$OUT_DIR/bootstub_shell.asm.tmp"
sed -i 's|build/loader.bin|build/loader_shell.bin|' "$OUT_DIR/bootstub_shell.asm.tmp"
nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -D"LOADER_OFFSET=$LOADER_OFF" \
     -i "$ROOT/" \
     -o "$OUT_DIR/bootstub_shell.bin" \
     -l "$OUT_DIR/bootstub_shell.lst" \
     "$OUT_DIR/bootstub_shell.asm.tmp"
rm -f "$OUT_DIR/bootstub_shell.asm.tmp"
echo "Built: $OUT_DIR/bootstub_shell.bin ($(wc -c < "$OUT_DIR/bootstub_shell.bin") bytes)"

# Demo transients added in Phase D — small period-flavored .COMs
# that show off the typed-prompt loader path and the AH=06h
# polling-input plumbing (SNAKE).
nasm -f bin -o "$OUT_DIR/primes.bin" -l "$OUT_DIR/primes.lst" "$SCRIPT_DIR/primes.asm"
echo "Built: $OUT_DIR/primes.bin ($(wc -c < "$OUT_DIR/primes.bin") bytes)"
nasm -f bin -o "$OUT_DIR/matrix.bin" -l "$OUT_DIR/matrix.lst" "$SCRIPT_DIR/matrix.asm"
echo "Built: $OUT_DIR/matrix.bin ($(wc -c < "$OUT_DIR/matrix.bin") bytes)"
nasm -f bin -o "$OUT_DIR/snake.bin"  -l "$OUT_DIR/snake.lst"  "$SCRIPT_DIR/snake.asm"
echo "Built: $OUT_DIR/snake.bin ($(wc -c < "$OUT_DIR/snake.bin") bytes)"

# CHKDSK.COM — period-correct disk verifier translated from Tim
# Paterson's CHKDSK_1981-07-15.ASM via scp_to_nasm.py (with R23/R24
# rules added for CHKDSK's `MOV B,reg,...` and `LABEL: EQU N` shapes).
# The translation goes through the same dialect translator the kernel
# does so the original source under Paterson-Listings is never edited.
CHKDSK_SRC=""
if [ -f "$ROOT/Paterson-Listings/3_source_code/PC-DOS_1.00_dev/CHKDSK_1981-07-15.ASM" ]; then
    CHKDSK_SRC="$ROOT/Paterson-Listings/3_source_code/PC-DOS_1.00_dev/CHKDSK_1981-07-15.ASM"
elif [ -f "$ROOT/../Paterson-Listings/3_source_code/PC-DOS_1.00_dev/CHKDSK_1981-07-15.ASM" ]; then
    CHKDSK_SRC="$ROOT/../Paterson-Listings/3_source_code/PC-DOS_1.00_dev/CHKDSK_1981-07-15.ASM"
fi
if [ -n "$CHKDSK_SRC" ]; then
    "$PYTHON" "$SCRIPT_DIR/scp_to_nasm.py" "$CHKDSK_SRC" \
        > "$OUT_DIR/chkdsk.translated.asm"
    nasm -f bin \
         -o "$OUT_DIR/chkdsk.bin" \
         -l "$OUT_DIR/chkdsk.lst" \
         "$OUT_DIR/chkdsk.translated.asm"
    echo "Built: $OUT_DIR/chkdsk.bin ($(wc -c < "$OUT_DIR/chkdsk.bin") bytes) [from Paterson 1981-07-15]"
else
    echo "Skipping CHKDSK.COM: source not found in Paterson-Listings" >&2
fi

# Confidence harness — exercises BIOSOUT/BIOSIN/BIOSREAD with
# deterministic output. Loaded at KERNEL_SEG:KERNEL_OFFSET so the
# same boot stub + firmware path drives it. Tests can assert
# byte-exact output to verify plumbing in isolation from the kernel.
nasm -f bin \
     -o "$OUT_DIR/hello.bin" \
     -l "$OUT_DIR/hello.lst" \
     "$SCRIPT_DIR/hello.asm"
echo "Built: $OUT_DIR/hello.bin ($(wc -c < "$OUT_DIR/hello.bin") bytes)"
