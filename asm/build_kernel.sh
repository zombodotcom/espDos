#!/bin/sh
# Build the 86-DOS 1.00 kernel from Tim Paterson's source.
# Tries kernel.patches.asm first (NASM-syntax wrapper); falls back to
# the raw source if no patches file exists.

set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM"
OUT_DIR="$ROOT/build"
mkdir -p "$OUT_DIR"

[ -f "$SRC" ] || { echo "Source not found: $SRC" >&2; exit 1; }

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
# emu8086.h so there's exactly one source of truth for those addresses.
HDR="$ROOT/firmware/components/emu8086/include/emu8086.h"
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

nasm -f bin \
     -D"KERNEL_SEG=$KSEG" \
     -D"KERNEL_OFFSET=$KOFF" \
     -o "$OUT_DIR/bootstub.bin" \
     -l "$OUT_DIR/bootstub.lst" \
     "$SCRIPT_DIR/bootstub.asm"
echo "Built: $OUT_DIR/bootstub.bin ($(wc -c < "$OUT_DIR/bootstub.bin") bytes) " \
     "[KERNEL_SEG=$KSEG KERNEL_OFFSET=$KOFF]"

# Confidence harness — exercises BIOSOUT/BIOSIN/BIOSREAD with
# deterministic output. Loaded at KERNEL_SEG:KERNEL_OFFSET so the
# same boot stub + firmware path drives it. Tests can assert
# byte-exact output to verify plumbing in isolation from the kernel.
nasm -f bin \
     -o "$OUT_DIR/hello.bin" \
     -l "$OUT_DIR/hello.lst" \
     "$SCRIPT_DIR/hello.asm"
echo "Built: $OUT_DIR/hello.bin ($(wc -c < "$OUT_DIR/hello.bin") bytes)"
