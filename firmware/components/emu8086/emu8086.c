/*
 * emu8086.c — ESP-IDF facade around 8086tiny.
 *
 * 8086tiny declares `mem` as a global pointer (patched from the
 * upstream's static array, since ~1.06 MB doesn't fit in the S3's
 * 256KB internal DRAM). We allocate it from PSRAM at startup and set
 * up the register-file pointers.
 *
 * Plan 2a: only memory + register init exposed. Instruction-stepping
 * API arrives in Plan 2b once we lift the for-loop body out of
 * 8086tiny's standalone main().
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "emu8086.h"

/* From 8086tiny.c. mem is a static array; regs8/regs16 are pointers
 * we wire to mem+REGS_BASE in emu_alloc_mem(). */
extern unsigned char  mem[];
extern unsigned char *regs8;
extern unsigned short *regs16;

/* Constants kept in sync with 8086tiny.c's (overrideable) defaults. */
#define EMU_RAM_SIZE_DEFAULT   0x30000u    /* 192 KB */
#define EMU_REGS_BASE_DEFAULT  0x20000u    /* register file at seg 0x2000 */

static const char *TAG = "emu8086";

esp_err_t emu_alloc_mem(void) {
    /* mem[] is a static array now (declared in 8086tiny.c). We just
     * need to wire up regs8/regs16 to point into it at REGS_BASE. */
    regs8  = mem + EMU_REGS_BASE_DEFAULT;
    regs16 = (unsigned short *)regs8;

    ESP_LOGI(TAG, "emu RAM static at %p (%u bytes), regs8=%p",
             (void *)mem, EMU_RAM_SIZE_DEFAULT, (void *)regs8);
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
        ESP_LOGE(TAG, "emu_load OOB: seg=%04x off=%04x n=%zu", seg, off, n);
        return;
    }
    memcpy(mem + phys, src, n);
}
