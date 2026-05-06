/*
 * espdos — main.
 *
 * Plan 1: ESP-IDF + QEMU pipeline working (banner over UART).
 * Plan 2a (here): emu8086 + kernel_blob components link cleanly.
 * Plan 2b: lift 8086tiny step API and actually run kernel instructions.
 */

#include <stdio.h>
#include <string.h>
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
    extern const uint8_t bootstub_bin_start[] asm("_binary_bootstub_bin_start");
    extern const uint8_t bootstub_bin_end[]   asm("_binary_bootstub_bin_end");
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

    /* MEMSCAN short-circuit patch.
     *
     * 86DOS.ASM line 3473-3483 walks segments writing-and-verifying
     * a probe byte to discover where RAM ends. On a real 8086 with
     * fewer than 1 MB installed, the verify fails at the first
     * unbacked segment. In our 8086tiny `mem[]` is a static C array
     * of size RAM_SIZE; writes past it go to neighboring BSS, so
     * every probe "succeeds" and MEMSCAN runs all 0xFFFF iterations
     * (≈ 650 000 instructions) before CX wraps. Even when it does
     * exit, it has corrupted whatever BSS lives just past mem[].
     *
     * Patch: at MEMSCAN entry, replace `INC CX; JZ HAVMEM; MOV DS,CX`
     * (5 bytes) with `MOV CX,0x3000; JMP HAVMEM` (also 5 bytes —
     * `B9 00 30 EB 0E`). The kernel sees ENDMEM = 0x3000 (= our
     * RAM_SIZE / 16) and continues to the banner-print code.
     *
     * This is brittle (depends on the kernel binary having MEMSCAN
     * at a specific offset). Located by signature search rather
     * than hardcoded address, so kernel rebuilds at slightly
     * different layouts still work. */
    {
        uint8_t *km = emu_mem();
        uint32_t kbase = ((uint32_t)EMU_KERNEL_SEG << 4) + EMU_KERNEL_OFFSET;
        size_t   ksize = kernel_blob_size();

        /* MEMSCAN signature after the preprocessor's R13b expansion of
         * the original `JZ HAVMEM` and `JZ MEMSCAN` into `Jnotcc; JMP`
         * pairs:
         *   41          INC CX
         *   75 02       JNZ +2
         *   EB 12       JMP HAVMEM
         *   8E D9       MOV DS, CX
         *   8A 07       MOV AL, [BX]
         *   F6 D0       NOT AL
         *   88 07       MOV [BX], AL
         *   3A 07       CMP AL, [BX]
         *   F6 D0       NOT AL
         *   88 07       MOV [BX], AL
         *   75 02       JNZ +2
         *   EB E9       JMP MEMSCAN
         */
        static const uint8_t sig[] = {
            0x41, 0x75, 0x02, 0xEB, 0x12, 0x8E, 0xD9, 0x8A, 0x07,
            0xF6, 0xD0, 0x88, 0x07, 0x3A, 0x07, 0xF6, 0xD0, 0x88,
            0x07, 0x75, 0x02, 0xEB, 0xE9
        };
        size_t found = (size_t)-1;
        for (size_t i = 0; i + sizeof sig <= ksize; i++) {
            if (memcmp(&km[kbase + i], sig, sizeof sig) == 0) {
                found = i; break;
            }
        }
        if (found == (size_t)-1) {
            ESP_LOGW(TAG, "MEMSCAN signature not found — kernel will spin "
                          "in MEMSCAN for ~10 seconds");
        } else {
            /* Replace the first 5 bytes (INC CX; JNZ +2; JMP HAVMEM)
             * with `MOV CX, 0x3000; JMP +0x12` — same length, same
             * fall-through target, but skips all the probe-write logic
             * and gives the kernel ENDMEM = our RAM_SIZE/16 directly. */
            uint32_t mp = kbase + found;
            km[mp + 0] = 0xB9;  /* MOV CX, imm16 */
            km[mp + 1] = 0x00;
            km[mp + 2] = 0x30;  /* CX = 0x3000 = RAM_SIZE/16 */
            km[mp + 3] = 0xEB;  /* JMP rel8 */
            km[mp + 4] = 0x12;  /* +18 → HAVMEM */
            ESP_LOGI(TAG, "patched MEMSCAN at offset 0x%04zx (CS:IP=%04x:%04zx) "
                          "→ ENDMEM=0x3000", found,
                     EMU_KERNEL_SEG, EMU_KERNEL_OFFSET + found);
        }
    }
#endif

    emu_init_state();
    emu_set_cs_ip(EMU_BOOT_SEG, EMU_BOOT_OFFSET);

    ESP_LOGI(TAG, "running emulator: CS:IP=%04x:%04x  ----- 86-DOS output below -----",
             emu_get_cs(), emu_get_ip());

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
