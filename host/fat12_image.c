#include <string.h>

#include "fat12_image.h"
#include "host_bios.h"

void fat12_init_empty_320kb(byte *image) {
    memset(image, 0, HOST_DISK_BYTES);

    /* FAT 1 starts at sector 1, FAT 2 at sector 3; each is 1 sector long
     * for this geometry (320 KB / 1024 bytes per cluster ~= 320 clusters ->
     * ~480 bytes of FAT12, fits in one 512-byte sector).  The kernel's
     * FATSIZ converges to 1 for these parameters.
     */
    const byte media = 0xFF;          /* DOS 1.0 320 KB DD */
    byte fat_head[3] = { media, 0xFF, 0xFF };

    memcpy(image + 1 * HOST_DISK_SECSIZ, fat_head, 3);  /* FAT 1 */
    memcpy(image + 3 * HOST_DISK_SECSIZ, fat_head, 3);  /* FAT 2 */
}
