#ifndef FAT12_IMAGE_H
#define FAT12_IMAGE_H

#include "dos_types.h"

/* Initialize the byte buffer as an empty, valid FAT12 320 KB DD volume:
 *   - boot sector (sector 0) zeroed
 *   - FAT 1 (sector 1) starts with media descriptor 0xFF + 0xFF 0xFF
 *     (entries 0 and 1 are reserved); rest zero
 *   - FAT 2 (sector 3) identical copy
 *   - root directory (sectors 5-11) zeroed (empty)
 *   - data area zeroed
 *
 * `image` must point to at least 320 KB (HOST_DISK_BYTES).
 */
void fat12_init_empty_320kb(byte *image);

#endif
