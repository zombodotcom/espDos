#!/usr/bin/env python3
# Build a minimal FAT12 floppy image for the 86-DOS kernel to read
# through bios_read / bios_write.
#
# 86-DOS 1.00 predates the BPB, so the kernel doesn't read disk
# geometry from sector 0 — geometry comes from the DPB table the
# bootloader hands to DOSINIT in DS:SI. We just produce a
# byte-aligned image whose layout matches what we tell the kernel:
#
#   sector 0          : boot sector (zeroed; kernel doesn't read it)
#   sectors 1..3      : FAT 1
#   sectors 4..6      : FAT 2 (mirror)
#   sectors 7..10     : root directory (64 × 32 B entries)
#   sectors 11..      : data area (cluster 2 starts at sector 11)
#
# Geometry: 360 KB single-sided 5.25" floppy
#   720 sectors × 512 B = 368640 bytes = 360 KB
#   1 reserved + 2 FATs × 3 + 4 root dir = 11 sector overhead
#   709 sectors of data area (~ 354 KB free)
#
# The output is padded to the partition size (default 384 KB) so
# `esptool.py write_flash` writes exactly partition_size bytes.
#
# This builder also adds files. Each file gets one root dir entry,
# its data is laid out starting at the first free cluster (cluster 2
# for the first file), and FAT entries are written so that the chain
# terminates with 0xFFF (EOC).

import struct
import sys
from pathlib import Path

SECTOR_SIZE      = 512
TOTAL_SECTORS    = 720         # 360 KB
RESERVED_SECTORS = 1
FAT_COUNT        = 2
SECTORS_PER_FAT  = 3
ROOT_ENTRIES     = 64
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE
SECTORS_PER_CLUSTER = 1
MEDIA_BYTE       = 0xFE        # 360 KB SS DD 5.25" — 86-DOS doesn't
                               # actually check this, but it's the
                               # right value for the geometry

PARTITION_SIZE   = 0x60000     # 384 KB — must match partitions.csv

# Files to embed in the image. Each entry is (8.3-name, source-path).
# We resolve each path relative to the repository root (one level up
# from this script). Missing files are skipped with a warning so the
# disk-image build never blocks an emulator iteration.
FILES = [
    # Order matters: each file occupies the next free cluster starting
    # from cluster 2. The transient loader hard-codes a sector number
    # (= cluster - 2 + data-area-start = cluster + 9), so changing the
    # order changes which loader variant finds which file. Current:
    #   HELLO.COM  -> cluster 2 -> sector 11 (loader.bin reads this)
    #   MANDEL.COM -> cluster 3 -> sector 12 (loader_mandel.bin reads)
    ("HELLO   ", "COM", "build/hellotr.bin"),
    ("MANDEL  ", "COM", "build/mandel.bin"),
    ("COUNT   ", "COM", "build/count.bin"),
    ("SHELL   ", "COM", "build/shell.bin"),
    ("JULIA   ", "COM", "build/julia.bin"),
    ("LIFE    ", "COM", "build/life.bin"),
    ("CHKDSK  ", "COM", "build/chkdsk.bin"),
    ("PRIMES  ", "COM", "build/primes.bin"),
    ("MATRIX  ", "COM", "build/matrix.bin"),
    ("SNAKE   ", "COM", "build/snake.bin"),
]


def fat12_set(fat: bytearray, n: int, value: int) -> None:
    """Set FAT12 entry n to value (12 bits)."""
    off = (n * 3) // 2
    if n & 1:
        # Odd entry: high nibble of byte[off], all of byte[off+1]
        fat[off]     = (fat[off] & 0x0F) | ((value & 0x0F) << 4)
        fat[off + 1] = (value >> 4) & 0xFF
    else:
        # Even entry: byte[off], low nibble of byte[off+1]
        fat[off]     = value & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)


def build_image(repo_root: Path):
    img = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    fat1_off = RESERVED_SECTORS * SECTOR_SIZE
    fat2_off = fat1_off + SECTORS_PER_FAT * SECTOR_SIZE
    root_off = fat2_off + SECTORS_PER_FAT * SECTOR_SIZE
    data_off = root_off + ROOT_DIR_SECTORS * SECTOR_SIZE

    # Build a single FAT and copy it to both slots once we're done.
    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat[0] = MEDIA_BYTE
    fat[1] = 0xFF
    fat[2] = 0xFF                  # entry 0 = 0xFFE, entry 1 = 0xFFF

    # Root directory and data are appended in one pass.
    next_cluster   = 2
    root_entry_idx = 0
    root_dir       = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    file_count     = 0

    for name8, ext3, src_rel in FILES:
        src = repo_root / src_rel
        if not src.is_file():
            print(f"  skip {src_rel}: not found (run asm/build_kernel.sh first)",
                  file=sys.stderr)
            continue
        if root_entry_idx >= ROOT_ENTRIES:
            print(f"  skip {src_rel}: root directory full", file=sys.stderr)
            continue
        data = src.read_bytes()

        # Allocate clusters and chain them in the FAT.
        clusters_needed = max(1, (len(data) + SECTOR_SIZE * SECTORS_PER_CLUSTER - 1)
                                  // (SECTOR_SIZE * SECTORS_PER_CLUSTER))
        first_cluster = next_cluster
        for i in range(clusters_needed):
            this_cluster = next_cluster
            next_cluster += 1
            if i == clusters_needed - 1:
                fat12_set(fat, this_cluster, 0xFFF)   # EOC
            else:
                fat12_set(fat, this_cluster, next_cluster)

        # Copy data into the data area at the right cluster offset.
        cluster_off = (first_cluster - 2) * SECTORS_PER_CLUSTER * SECTOR_SIZE
        img[data_off + cluster_off : data_off + cluster_off + len(data)] = data

        # Write the 32-byte directory entry. Layout per 86-DOS docs:
        #   0..10  filename (8) + extension (3), space-padded
        #   11     attribute (0x20 = archive)
        #   12..21 reserved (zero)
        #   22..23 time
        #   24..25 date
        #   26..27 starting cluster
        #   28..31 file size
        entry = bytearray(32)
        full = (name8 + ext3).encode('ascii')
        assert len(full) == 11, f"bad name length: {full!r}"
        entry[0:11] = full
        entry[11]   = 0x20            # ARCHIVE
        # time, date stay zero — 86-DOS 1.0 doesn't care.
        struct.pack_into('<H', entry, 26, first_cluster)
        struct.pack_into('<I', entry, 28, len(data))
        root_dir[root_entry_idx * 32 : (root_entry_idx + 1) * 32] = entry
        root_entry_idx += 1
        file_count += 1
        print(f"  added {src_rel} as {name8}.{ext3}: "
              f"cluster {first_cluster}, {len(data)} bytes")

    # Stitch the constructed FAT and root dir into the image.
    img[fat1_off : fat1_off + len(fat)] = fat
    img[fat2_off : fat2_off + len(fat)] = fat
    img[root_off : root_off + len(root_dir)] = root_dir

    # Pad to partition size.
    if len(img) < PARTITION_SIZE:
        img += b'\xFF' * (PARTITION_SIZE - len(img))

    return bytes(img), file_count


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else Path('build/disk.img')
    repo_root = Path(__file__).resolve().parent.parent
    out.parent.mkdir(parents=True, exist_ok=True)
    data, file_count = build_image(repo_root)
    out.write_bytes(data)
    print(f'wrote {out} ({len(data)} bytes; '
          f'{TOTAL_SECTORS} sectors of disk + pad to {PARTITION_SIZE} B; '
          f'{file_count} embedded file(s))')


if __name__ == '__main__':
    main(sys.argv)
