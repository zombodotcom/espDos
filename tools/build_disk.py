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
#   sectors 11..      : data area
#
# Geometry: 360 KB single-sided 5.25" floppy
#   720 sectors × 512 B = 368640 bytes = 360 KB
#   1 reserved + 2 FATs × 3 + 4 root dir = 11 sector overhead
#   709 sectors of data area (~ 354 KB free)
#
# The output is padded to the partition size (default 384 KB) so
# `esptool.py write_flash` writes exactly partition_size bytes.

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
MEDIA_BYTE       = 0xFE        # 360 KB SS DD 5.25" — 86-DOS doesn't
                               # actually check this, but it's the
                               # right value for the geometry

PARTITION_SIZE   = 0x60000     # 384 KB — must match partitions.csv

def build_image():
    img = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    # FAT 1 starts at sector RESERVED_SECTORS. First two FAT12 entries
    # are reserved: entry[0] = media byte | 0xFFFFFF00, entry[1] = EOC
    # (end-of-chain). For an empty disk that's all the FAT contains.
    fat1 = RESERVED_SECTORS * SECTOR_SIZE
    img[fat1 + 0] = MEDIA_BYTE
    img[fat1 + 1] = 0xFF
    img[fat1 + 2] = 0xFF

    # FAT 2 mirrors FAT 1 (the kernel writes both during normal ops;
    # for an empty disk we just initialize them identically).
    fat2 = fat1 + SECTORS_PER_FAT * SECTOR_SIZE
    img[fat2 + 0] = MEDIA_BYTE
    img[fat2 + 1] = 0xFF
    img[fat2 + 2] = 0xFF

    # Root directory: stays all zeros (= empty entries).
    # Data area: stays all zeros.

    # Pad to partition size. esp_partition_write expects writes
    # aligned to flash erase boundaries; padding with 0xFF mirrors
    # what the flash looks like after an erase.
    if len(img) < PARTITION_SIZE:
        img += b'\xFF' * (PARTITION_SIZE - len(img))

    return bytes(img)

def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else Path('build/disk.img')
    out.parent.mkdir(parents=True, exist_ok=True)
    data = build_image()
    out.write_bytes(data)
    print(f'wrote {out} ({len(data)} bytes; '
          f'{TOTAL_SECTORS} sectors of disk + pad to {PARTITION_SIZE} B)')

if __name__ == '__main__':
    main(sys.argv)
