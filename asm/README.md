# Kernel build

Sources:
- `../../Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM` — Tim
  Paterson's 86-DOS 1.00 kernel; treated as **read-only**.
- `kernel.patches.asm` — NASM-syntax adjustments only; no logic changes.
  Each adjustment tagged with `; PATCH (NASM): <reason>` so a re-vendor
  can be diffed precisely.

Build:

```
./build_kernel.sh
```

Output: `../build/kernel.bin` (flat binary) + `../build/kernel.lst` (NASM
listing — used later to map runtime PC back to source lines).
