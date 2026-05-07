/*
 * esp8086_facade.c — ESP-IDF facade around the esp8086 interpreter.
 *
 * esp8086.c declares mem[] as a static array sized RAM_SIZE = 0x110000
 * (1 MB + 64 KB margin), placed in PSRAM via EXT_RAM_BSS_ATTR on
 * ESP_PLATFORM. This facade just wires regs8/regs16 to point at
 * REGS_BASE inside that array — there is no heap allocation, so
 * emu_alloc_mem() can never fail to allocate.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

#include "esp8086.h"

/* From esp8086.c. mem is a static array; regs8/regs16 are pointers we
 * wire to mem+REGS_BASE in emu_alloc_mem(). */
extern unsigned char  mem[];
extern unsigned char *regs8;
extern unsigned short *regs16;

#define EMU_RAM_SIZE_DEFAULT  0x110000u   /* must match RAM_SIZE in esp8086.c */

#ifdef ESP_PLATFORM
static const char *TAG = "esp8086";
#endif

esp_err_t emu_alloc_mem(void) {
    /* Defensively zero the entire emulated RAM. Static .ext_ram.bss is
     * supposed to be zeroed by ESP-IDF's bootloader, but if it isn't
     * (older IDF, custom bootloader, etc.) the PSP at USER_SEG:0000
     * that the kernel reads on every INT 21h call would contain
     * garbage, and the transient would crash silently. ~1 MB memset
     * costs a couple of ms at boot — not worth being clever about. */
    memset(mem, 0, EMU_RAM_SIZE_DEFAULT);

    regs8  = mem + EMU_REGS_BASE;
    regs16 = (unsigned short *)regs8;

#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "emu RAM static at %p (%u bytes, zeroed), regs8=%p",
             (void *)mem, EMU_RAM_SIZE_DEFAULT, (void *)regs8);
#endif
    return ESP_OK;
}

size_t emu_ram_size(void) {
    return EMU_RAM_SIZE_DEFAULT;
}

uint8_t *emu_mem(void) {
    return (uint8_t *)mem;
}

void emu_load(uint16_t seg, uint16_t off, const void *src, size_t n) {
    uint32_t phys = ((uint32_t)seg << 4) + off;
    if (phys + n > EMU_RAM_SIZE_DEFAULT) {
#ifdef ESP_PLATFORM
        ESP_LOGE(TAG, "emu_load OOB: seg=%04x off=%04x n=%zu", seg, off, n);
#endif
        return;
    }
    memcpy(mem + phys, src, n);
}
