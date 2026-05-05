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

# Translate SCP-ASM dialect to NASM via the preprocessor. The
# original $SRC is never modified.
TRANSLATED="$OUT_DIR/kernel.translated.asm"
python "$SCRIPT_DIR/scp_to_nasm.py" "$SRC" > "$TRANSLATED"

nasm -f bin -o "$OUT_DIR/kernel.bin" -l "$OUT_DIR/kernel.lst" "$TRANSLATED"
echo "Built: $OUT_DIR/kernel.bin ($(wc -c < "$OUT_DIR/kernel.bin") bytes)"
