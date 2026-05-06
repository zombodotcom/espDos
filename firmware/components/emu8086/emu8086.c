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

/* From 8086tiny.c. */
extern unsigned char *mem;          /* allocated below                */
extern unsigned char *regs8;        /* points at mem + REGS_BASE      */
extern unsigned short *regs16;      /* word-aliased over regs8        */

/* Constants kept in sync with 8086tiny.c's (overrideable) defaults. */
#define EMU_RAM_SIZE_DEFAULT   0x40000u    /* 256 KB */
#define EMU_REGS_BASE_DEFAULT  0x3FFC0u    /* near top of mem */

static const char *TAG = "emu8086";

esp_err_t emu_alloc_mem(void) {
    if (mem != NULL) return ESP_OK;

    /* Try PSRAM first (8MB on T-Display-S3 hardware). On QEMU S3 PSRAM
     * is not emulated, so fall back to internal DRAM — fine because we
     * set RAM_SIZE = 256 KB which fits there. */
    mem = (unsigned char *)heap_caps_calloc(1, EMU_RAM_SIZE_DEFAULT,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == NULL) {
        ESP_LOGI(TAG, "PSRAM unavailable, using internal DRAM");
        mem = (unsigned char *)heap_caps_calloc(1, EMU_RAM_SIZE_DEFAULT,
                MALLOC_CAP_8BIT);
    }
    if (mem == NULL) {
        ESP_LOGE(TAG, "could not allocate %u bytes for emu RAM",
                 EMU_RAM_SIZE_DEFAULT);
        return ESP_ERR_NO_MEM;
    }

    /* 8086tiny puts its register file at REGS_BASE inside mem. */
    regs8  = mem + EMU_REGS_BASE_DEFAULT;
    regs16 = (unsigned short *)regs8;

    ESP_LOGI(TAG, "allocated %u bytes at %p (regs8=%p)",
             EMU_RAM_SIZE_DEFAULT, (void *)mem, (void *)regs8);
    return ESP_OK;
}

size_t emu_ram_size(void) {
    return EMU_RAM_SIZE_DEFAULT;
}

const uint8_t *emu_ram(void) {
    return (const uint8_t *)mem;
}
