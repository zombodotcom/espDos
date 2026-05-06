/*
 * espdos — main.
 *
 * Plan 1: ESP-IDF + QEMU pipeline working (banner over UART).
 * Plan 2a (here): emu8086 + kernel_blob components link cleanly.
 * Plan 2b: lift 8086tiny step API and actually run kernel instructions.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"

#include "emu8086.h"
#include "kernel_blob.h"

static const char *TAG = "espdos";

static void hex_dump_first(const char *label, const uint8_t *p, size_t n) {
    char line[64];
    int  off = 0;
    for (size_t i = 0; i < n; i++) {
        off += snprintf(line + off, sizeof(line) - off, "%02x ", p[i]);
        if (off >= (int)sizeof(line) - 4) break;
    }
    ESP_LOGI(TAG, "%s: %s", label, line);
}

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  +---------------------------------------+");
    ESP_LOGI(TAG, "  |  espDos - MS-DOS 1.0 on ESP32         |");
    ESP_LOGI(TAG, "  |  Tim Paterson's 86-DOS, in your chip  |");
    ESP_LOGI(TAG, "  +---------------------------------------+");
    ESP_LOGI(TAG, "");

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Chip: %s rev %d, %d cores, features 0x%lx",
             CONFIG_IDF_TARGET, chip.revision,
             chip.cores, (unsigned long)chip.features);

    /* Plan 2a: confirm both components are linked + the kernel blob is
     * embedded with the expected content. */
    size_t klen = kernel_blob_size();
    ESP_LOGI(TAG, "kernel_blob: %zu bytes embedded "
                  "(expecting ~5861 from 86DOS.ASM)", klen);
    hex_dump_first("kernel[0..15]", kernel_bin_start, 16);

    /* Allocate the emulator's 320 KB memory from PSRAM/DRAM. */
    esp_err_t err = emu_alloc_mem();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "emu_alloc_mem failed: %s", esp_err_to_name(err));
        goto idle;
    }
    ESP_LOGI(TAG, "emu8086 ram: %zu bytes at %p", emu_ram_size(),
             (void *)emu_mem());

    /* Plan 2b: load 8086tiny BIOS at REGS_BASE+0x100 (segment 0x4000,
     * offset 0x100) so the instruction decoder's lookup tables work,
     * then load Tim Paterson's kernel at KERNEL_SEG:0 and run. */
    extern const uint8_t bios_bin_start[] asm("_binary_bios_bin_start");
    extern const uint8_t bios_bin_end[]   asm("_binary_bios_bin_end");
    size_t bios_len = (size_t)(bios_bin_end - bios_bin_start);
    ESP_LOGI(TAG, "loading 8086tiny BIOS: %zu bytes at 2000:0100",
             bios_len);
    /* REGS_BASE=0x20000 in 8086tiny; segment 0x2000 maps to that. */
    emu_load(0x2000, 0x0100, bios_bin_start, bios_len);

    /* Verify BIOS landed at the right place. */
    uint8_t *m = emu_mem();
    ESP_LOGI(TAG, "mem[0x20100..0x20107] = %02x %02x %02x %02x %02x %02x %02x %02x",
             m[0x20100], m[0x20101], m[0x20102], m[0x20103],
             m[0x20104], m[0x20105], m[0x20106], m[0x20107]);

    emu_load_bios_tables();
    /* Spot-check a few populated table entries. TABLE_BASE_INST_SIZE
     * should be table 0; for opcode 0xE9 (JMP near) the size is 3. */
    extern unsigned char bios_table_lookup[20][256];
    ESP_LOGI(TAG, "bios_table_lookup[0][0xE9]=0x%02x [0][0x00]=0x%02x "
                  "[1][0xE9]=0x%02x",
             bios_table_lookup[0][0xE9], bios_table_lookup[0][0x00],
             bios_table_lookup[1][0xE9]);

    /* Load kernel at KERNEL_SEG:0. DOS 1.0 originally loaded at
     * a low segment after the bootstrap; we pick 0x0100 (= phys
     * 0x1000), which leaves the IVT and BIOS data area at low
     * memory and matches the kernel's `org 100h` (the assembled
     * kernel.bin starts with JMP DOSINIT relative to 0). */
    const uint16_t KERNEL_SEG = 0x0100;
    ESP_LOGI(TAG, "loading kernel: %zu bytes at %04x:0000",
             kernel_blob_size(), KERNEL_SEG);
    emu_load(KERNEL_SEG, 0x0000, kernel_bin_start, kernel_blob_size());

    /* Initialize CPU state and set the entry point. */
    emu_init_state();
    emu_set_cs_ip(KERNEL_SEG, 0x0000);

    ESP_LOGI(TAG, "running emulator: CS:IP=%04x:%04x",
             emu_get_cs(), emu_get_ip());

    /* Run in chunks, logging CS:IP periodically. */
    int total = 0;
    for (int chunk = 0; chunk < 10; chunk++) {
        int still_running = emu_run_n(50);
        total += 50;
        ESP_LOGI(TAG, "after %d steps: CS:IP=%04x:%04x AX=%04x running=%d",
                 total, emu_get_cs(), emu_get_ip(), emu_get_ax(),
                 still_running);
        if (!still_running) {
            ESP_LOGI(TAG, "emulator halted (CS:IP=0:0)");
            break;
        }
    }

idle:
    /* Idle. Plan 3 will replace this with full BIOS dispatch. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
