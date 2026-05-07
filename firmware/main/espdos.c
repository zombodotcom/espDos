/*
 * espdos — main.
 *
 * Plan 1: ESP-IDF + QEMU pipeline working (banner over UART).
 * Plan 2a (here): esp8086 + kernel_blob components link cleanly.
 * Plan 2b: lift 8086tiny step API and actually run kernel instructions.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"

#include "esp8086.h"
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
                  "(expecting ~6341 from 86DOS.ASM after R13b expansion)", klen);
    hex_dump_first("kernel[0..15]", kernel_bin_start, 16);

    /* Install UART driver so bios_in / bios_out can read+write
     * with proper FreeRTOS yielding (otherwise the task watchdog
     * fires when the kernel blocks on input). */
    bios_init();

    /* Wire up regs8/regs16 pointers into esp8086's static mem[]
     * (1 MB + 64 KB margin, in PSRAM via EXT_RAM_BSS_ATTR). */
    esp_err_t err = emu_alloc_mem();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "emu_alloc_mem failed: %s", esp_err_to_name(err));
        goto idle;
    }
    ESP_LOGI(TAG, "esp8086 ram: %zu bytes at %p", emu_ram_size(),
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
    uint32_t bios_phys = ((uint32_t)EMU_BIOS_SEG << 4) + EMU_BIOS_OFFSET;
    ESP_LOGI(TAG, "mem[BIOS..BIOS+7] @ 0x%05lx = %02x %02x %02x %02x %02x %02x %02x %02x",
             (unsigned long)bios_phys,
             m[bios_phys + 0], m[bios_phys + 1], m[bios_phys + 2], m[bios_phys + 3],
             m[bios_phys + 4], m[bios_phys + 5], m[bios_phys + 6], m[bios_phys + 7]);

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

    /* Load the boot stub (8086 ASM, assembled alongside the kernel).
     * It does what a real boot loader would have done before handing
     * control to 86-DOS:
     *   - install IVT entries for the exit-class vectors (20/22/23/24/27)
     *     so any clean program-end lands in a known halt loop
     *   - set DS:SI to a Drive Parameter Block init table that 86-DOS
     *     reads on its very first instruction (NUMDRV + per-drive DPT)
     *   - JMP FAR to KERNEL_SEG:KERNEL_OFFSET
     * Without the DPB, NUMDRV ends up zero, no drives get configured,
     * and the kernel's later disk + exit paths corrupt themselves. */
    /* Use the mandel-loader variant of the bootstub so the demo loads
     * MANDEL.COM (impressive output) instead of HELLO.COM ("transient
     * ok"). Both bootstubs are byte-identical except for the embedded
     * LOAD_SECTOR — see asm/build_kernel.sh for how they're built.
     * Define ESPDOS_LOADER_HELLO to swap back. */
#if defined(ESPDOS_LOADER_HELLO)
    extern const uint8_t bootstub_bin_start[] asm("_binary_bootstub_bin_start");
    extern const uint8_t bootstub_bin_end[]   asm("_binary_bootstub_bin_end");
#elif defined(ESPDOS_LOADER_COUNT)
    extern const uint8_t bootstub_bin_start[] asm("_binary_bootstub_count_bin_start");
    extern const uint8_t bootstub_bin_end[]   asm("_binary_bootstub_count_bin_end");
#elif defined(ESPDOS_LOADER_SHELL)
    extern const uint8_t bootstub_bin_start[] asm("_binary_bootstub_shell_bin_start");
    extern const uint8_t bootstub_bin_end[]   asm("_binary_bootstub_shell_bin_end");
#else
    extern const uint8_t bootstub_bin_start[] asm("_binary_bootstub_mandel_bin_start");
    extern const uint8_t bootstub_bin_end[]   asm("_binary_bootstub_mandel_bin_end");
#endif
    size_t bootstub_len = (size_t)(bootstub_bin_end - bootstub_bin_start);
    ESP_LOGI(TAG, "loading boot stub: %zu bytes at %04x:%04x",
             bootstub_len, EMU_BOOT_SEG, EMU_BOOT_OFFSET);
    emu_load(EMU_BOOT_SEG, EMU_BOOT_OFFSET,
             bootstub_bin_start, bootstub_len);

    /* Choose the payload that runs at KERNEL_SEG:KERNEL_OFFSET.
     * Default: the real 86-DOS kernel. With ESPDOS_PAYLOAD_HELLO
     * defined (via `idf.py build -DESPDOS_PAYLOAD_HELLO=1`), we load
     * asm/hello.bin instead — a small program that exercises every
     * BIOSSEG entry our handlers care about, with deterministic
     * output. Use it to confirm hardware-side plumbing matches the
     * host test_hello result before chasing kernel issues. */
#ifdef ESPDOS_PAYLOAD_HELLO
    ESP_LOGI(TAG, "loading hello payload: %zu bytes at %04x:%04x  "
                  "(confidence harness — set ESPDOS_PAYLOAD_HELLO=0 "
                  "to run the real kernel)",
             hello_blob_size(), EMU_KERNEL_SEG, EMU_KERNEL_OFFSET);
    emu_load(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET,
             hello_bin_start, hello_blob_size());
#else
    ESP_LOGI(TAG, "loading kernel: %zu bytes at %04x:%04x",
             kernel_blob_size(), EMU_KERNEL_SEG, EMU_KERNEL_OFFSET);
    emu_load(EMU_KERNEL_SEG, EMU_KERNEL_OFFSET,
             kernel_bin_start, kernel_blob_size());
#endif

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    ESP_LOGI(TAG, "running emulator: CS:IP=%04x:%04x  ----- 86-DOS output below -----",
             emu_get_cs(), emu_get_ip());

    ESP_LOGI(TAG, "running MEMSCAN... (~650K instructions to scan 1 MB)");

    /* Run the emulator in 5000-instruction beats with a FreeRTOS yield
     * between, so the IDLE task can pet the watchdog. Per-beat heartbeat
     * logging was useful while chasing emulator bugs and is now silent;
     * rebuild with -DESPDOS_HEARTBEAT=1 to bring it back. */
    for (int beat = 0; beat < 2000; beat++) {
        int still_running = emu_run_n(5000);
#ifdef ESPDOS_HEARTBEAT
        ESP_LOGI(TAG, "heartbeat %d: %d steps  CS:IP=%04x:%04x  AX=%04x",
                 beat, (beat + 1) * 5000,
                 emu_get_cs(), emu_get_ip(), emu_get_ax());
#endif
        if (!still_running) {
            ESP_LOGI(TAG, "----- emulator halted (CS:IP=0:0) -----");
            break;
        }
        vTaskDelay(1);
    }

idle:
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
