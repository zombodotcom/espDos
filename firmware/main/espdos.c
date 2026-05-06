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

    /* Allocate the emulator's 1.06 MB memory from PSRAM. */
    esp_err_t err = emu_alloc_mem();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "emu_alloc_mem failed: %s", esp_err_to_name(err));
    }
    size_t rsz = emu_ram_size();
    const uint8_t *ram = emu_ram();
    ESP_LOGI(TAG, "emu8086 ram: %zu bytes at %p", rsz, (void *)ram);

    /* Sanity check: kernel should start with JMP near (0xE9) per
     * `nasm -l kernel.lst` showing JMP DOSINIT at offset 0. */
    if (kernel_bin_start[0] == 0xE9u) {
        ESP_LOGI(TAG, "Plan 2a OK: kernel embeds correctly, "
                      "first byte is JMP (0xE9) as expected.");
    } else {
        ESP_LOGE(TAG, "Plan 2a FAIL: kernel[0] = 0x%02x, expected 0xE9",
                 kernel_bin_start[0]);
    }

    /* Idle. Plan 2b replaces this with the emu_step loop. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
