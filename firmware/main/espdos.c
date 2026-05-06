/*
 * espdos — main.
 *
 * Plan 1 milestone: prove the ESP-IDF + QEMU toolchain pipeline works.
 * Just print a banner over UART and idle. Subsequent plans wire in the
 * 8086 emulator, BIOS handlers, flash disk, and (eventually) WebSocket.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"

static const char *TAG = "espdos";

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

    ESP_LOGI(TAG, "Plan 1 milestone reached. Halting in idle loop.");

    /* Idle forever. Plans 2+ replace this with the emulator main loop. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
