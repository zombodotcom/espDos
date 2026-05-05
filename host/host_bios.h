#ifndef HOST_BIOS_H
#define HOST_BIOS_H

#include "bios.h"

/* Standard 320 KB DD floppy: 640 sectors * 512 bytes */
#define HOST_DISK_SECSIZ   512u
#define HOST_DISK_DSKSIZ   640u
#define HOST_DISK_BYTES    (HOST_DISK_SECSIZ * HOST_DISK_DSKSIZ)

/* The shared RAM-backed disk image, exported so tests can reset/inspect it. */
extern byte host_disk_image[HOST_DISK_BYTES];

/* Populated BIOS vtable: stdio console, RAM-backed disk, no printer/aux. */
extern bios_vtable_t host_bios;

/* Init table for one drive (drive 0 = 320 KB DD); points into a static
 * buffer owned by host_bios.c. Pass straight to dos_init(). */
byte *host_init_table(void);

#endif
