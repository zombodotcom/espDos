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
#include "bios.h"

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

    /* Install UART driver so bios_in / bios_out can read+write
     * with proper FreeRTOS yielding (otherwise the task watchdog
     * fires when the kernel blocks on input). */
    bios_init();

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
    ESP_LOGI(TAG, "loading 8086tiny BIOS: %zu bytes at %04x:%04x",
             bios_len, EMU_BIOS_SEG, EMU_BIOS_OFFSET);
    emu_load(EMU_BIOS_SEG, EMU_BIOS_OFFSET, bios_bin_start, bios_len);

    /* Verify BIOS landed at the right place. */
    uint8_t *m = emu_mem();
    ESP_LOGI(TAG, "mem[0x20100..0x20107] = %02x %02x %02x %02x %02x %02x %02x %02x",
             m[0x20100], m[0x20101], m[0x20102], m[0x20103],
             m[0x20104], m[0x20105], m[0x20106], m[0x20107]);

    emu_load_bios_tables();
    /* Spot-check populated tables for opcode 0xE9 (JMP near).
     * TABLE_XLAT_OPCODE = 8, BASE_INST_SIZE = 12, I_W_SIZE = 13.
     * Expected: XLAT[0xE9] = 14 (decodes as OPCODE 14 = JMP/CALL). */
    extern unsigned char bios_table_lookup[20][256];
    ESP_LOGI(TAG, "table[8][0xE9]=0x%02x  (XLAT_OPCODE, expect 14)",
             bios_table_lookup[8][0xE9]);
    ESP_LOGI(TAG, "table[12][0xE9]=0x%02x  table[13][0xE9]=0x%02x  "
                  "(BASE_INST_SIZE, I_W_SIZE)",
             bios_table_lookup[12][0xE9], bios_table_lookup[13][0xE9]);

    ESP_LOGI(TAG, "loading kernel: %zu bytes at %04x:%04x",
             kernel_blob_size(), EMU_KERNEL_SEG, EMU_KERNEL_OFFSET);
    emu_load(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET,
             kernel_bin_start, kernel_blob_size());

    emu_init_state();
    emu_set_cs_ip(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET);

    ESP_LOGI(TAG, "running emulator: CS:IP=%04x:%04x  ----- 86-DOS output below -----",
             emu_get_cs(), emu_get_ip());

    /* Run in 5K-instruction chunks; log CS:IP between chunks so we
     * can see whether the kernel is making forward progress or stuck
     * in a tight loop. Once we trust this, drop the heartbeat. */
    uint64_t total_steps = 0;
    uint16_t prev_ip = emu_get_ip();
    int      same_ip_count = 0;
    for (int beat = 0; beat < 200; beat++) {
        int still_running = emu_run_n(100);
        total_steps += 100;
        uint16_t cs = emu_get_cs(), ip = emu_get_ip();
        ESP_LOGI(TAG, "heartbeat %d: %llu steps  CS:IP=%04x:%04x  AX=%04x",
                 beat, (unsigned long long)total_steps, cs, ip, emu_get_ax());
        if (!still_running) {
            ESP_LOGI(TAG, "----- emulator halted (CS:IP=0:0) -----");
            break;
        }
        if (ip == prev_ip && cs == emu_get_cs()) {
            if (++same_ip_count >= 3) {
                ESP_LOGW(TAG, "IP stuck at %04x:%04x for 3 heartbeats — "
                              "tight loop or hang", cs, ip);
                break;
            }
        } else {
            same_ip_count = 0;
        }
        prev_ip = ip;
        vTaskDelay(1);
    }

idle:
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
