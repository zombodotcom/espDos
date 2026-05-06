/*
 * bios.c — Plan 3 BIOS handlers (console first; disk + printer + aux
 * are stubbed and just log so we can see what the kernel asks for).
 *
 * Console I/O routes through ESP-IDF's stdio, which goes to the UART
 * by default. Plan 4 will swap stdio for WebSocket-driven streams.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bios.h"

/* The T-Display-S3's USB-C is wired to the chip's native USB-Serial-JTAG
 * peripheral, NOT to UART0 (UART0 only goes to physical TX/RX pins).
 * ESP-IDF logs land on BOTH (CONSOLE primary=UART0, secondary=JTAG)
 * which is why we see them, but for actual interactive I/O we must
 * read from the JTAG side or input never reaches the chip. */
#define BIOS_RX_BUF    256
#define BIOS_TX_BUF    256

void bios_init(void) {
    static int installed = 0;
    if (installed) return;
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = BIOS_RX_BUF,
        .tx_buffer_size = BIOS_TX_BUF,
    };
    usb_serial_jtag_driver_install(&cfg);
    installed = 1;
}

/* From 8086tiny.c via emu8086 component. */
extern unsigned char  *regs8;
extern unsigned short *regs16;

/* 8086tiny register indices (from 8086tiny.c). */
#define REG_AL  0
#define REG_AH  1
#define REG_AX  0
#define REG_CX  1
#define REG_DX  2
#define REG_BX  3
#define REG_ES  8
#define REG_DS  11

#define FLAG_CF 40

static const char *TAG = "bios";

/* AL/AH access helpers — regs8 is byte-aliased over regs16 starting
 * at REGS_BASE. Layout (from 8086tiny conventions):
 *   regs8[REG_AL*2]   = AL
 *   regs8[REG_AL*2+1] = AH (= REG_AH index points at +1 byte)
 * For low/high byte access we use even/odd offsets. */
static inline uint8_t  rd_al(void) { return regs8[0]; }
static inline uint8_t  rd_ah(void) { return regs8[1]; }
static inline void     wr_al(uint8_t v) { regs8[0] = v; }
static inline void     wr_ah(uint8_t v) { regs8[1] = v; }
static inline uint16_t rd_ax(void) { return regs16[REG_AX]; }
static inline void     wr_ax(uint16_t v) { regs16[REG_AX] = v; }

/* Set/clear the carry flag in 8086tiny's flag-byte storage. */
static inline void set_cf(int cf) { regs8[FLAG_CF] = cf ? 1 : 0; }


/* ===== Console handlers — USB-Serial-JTAG-backed.
 * On the T-Display-S3 the USB-C is the JTAG peripheral; reading/
 * writing here puts bytes on the same channel as idf.py monitor. */

static uint8_t bios_stat(void) {
    /* Try a 0-tick read; if it returns 1, push back into our 1-byte
     * peek buffer so the next bios_in() picks it up. */
    static uint8_t peek;
    static int peek_valid = 0;
    if (peek_valid) return 0xFF;
    int n = usb_serial_jtag_read_bytes(&peek, 1, 0);
    if (n == 1) { peek_valid = 1; return 0xFF; }
    return 0;
}

static uint8_t bios_in(void) {
    extern int  usb_serial_jtag_read_bytes(void *, uint32_t, TickType_t);
    /* Cooperative blocking: 100ms read with retry. JTAG driver yields
     * during the wait, so the watchdog stays satisfied. */
    uint8_t ch;
    while (1) {
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
        if (n == 1) return ch;
        vTaskDelay(1);
    }
}

static void bios_out(uint8_t ch) {
    usb_serial_jtag_write_bytes(&ch, 1, pdMS_TO_TICKS(20));
}


/* ===== Disk + printer + aux: stubs (no-op for printer/aux, error for
 * disk). Counters expose call frequency in the log. =========== */

static unsigned counter[10];   /* indexed by IP/3 — small + cheap */

static void bios_print(uint8_t ch) { (void)ch; }
static uint8_t bios_auxin(void)    { return 0x1A; }
static void bios_auxout(uint8_t c) { (void)c; }

static int bios_read(uint8_t drive, uint16_t buf_off, uint16_t count,
                     uint16_t sector) {
    (void)drive; (void)buf_off; (void)count; (void)sector;
    return 1;
}
static int bios_write(uint8_t drive, uint16_t buf_off, uint16_t count,
                      uint16_t sector) {
    (void)drive; (void)buf_off; (void)count; (void)sector;
    return 1;
}
static int bios_dskchg(uint8_t drive) { (void)drive; return 1; }


/* ===== Top-level dispatch: route on the offset within BIOSSEG. ===== */

/* Periodically dump call counts so we can see the call mix without
 * spamming the log on every char. */
static void maybe_dump_counts(uint16_t ip) {
    int idx = ip / 3;
    if (idx >= 0 && idx < 10) counter[idx]++;
    /* Print a summary every 256 BIOS calls. */
    static unsigned total;
    if ((++total & 0xFF) == 0) {
        ESP_LOGI(TAG, "bios calls: stat=%u in=%u out=%u print=%u "
                      "axin=%u axout=%u rd=%u wr=%u dskchg=%u",
                 counter[1], counter[2], counter[3], counter[4],
                 counter[5], counter[6], counter[7], counter[8],
                 counter[9]);
    }
}

void bios_handle_call(uint16_t ip)
{
    maybe_dump_counts(ip);

    /* Log the first 500 BIOS calls verbatim so we can see exactly what
     * the kernel is asking for. After 500 the counters take over via
     * maybe_dump_counts. */
    static int verbose_left = 500;
    if (verbose_left > 0) {
        verbose_left--;
        const char *name = "?";
        switch (ip) {
        case BIOS_OFF_STAT:   name = "STAT";   break;
        case BIOS_OFF_IN:     name = "IN";     break;
        case BIOS_OFF_OUT:    name = "OUT";    break;
        case BIOS_OFF_PRINT:  name = "PRINT";  break;
        case BIOS_OFF_AUXIN:  name = "AUXIN";  break;
        case BIOS_OFF_AUXOUT: name = "AUXOUT"; break;
        case BIOS_OFF_READ:   name = "READ";   break;
        case BIOS_OFF_WRITE:  name = "WRITE";  break;
        case BIOS_OFF_DSKCHG: name = "DSKCHG"; break;
        }
        ESP_LOGI(TAG, "BIOS call: %-7s ip=%02x AL=%02x AH=%02x AX=%04x BX=%04x",
                 name, ip, regs8[0], regs8[1],
                 regs16[REG_AX], regs16[REG_BX]);
    }

    switch (ip) {
    case BIOS_OFF_STAT:
        wr_al(bios_stat());
        break;
    case BIOS_OFF_IN:
        wr_al(bios_in());
        break;
    case BIOS_OFF_OUT:
        bios_out(rd_al());
        break;
    case BIOS_OFF_PRINT:
        bios_print(rd_al());
        break;
    case BIOS_OFF_AUXIN:
        wr_al(bios_auxin());
        break;
    case BIOS_OFF_AUXOUT:
        bios_auxout(rd_al());
        break;
    case BIOS_OFF_READ: {
        int err = bios_read(rd_al(), regs16[REG_BX],
                            regs16[REG_CX], regs16[REG_DX]);
        set_cf(err);
        break;
    }
    case BIOS_OFF_WRITE: {
        int err = bios_write(rd_al(), regs16[REG_BX],
                             regs16[REG_CX], regs16[REG_DX]);
        set_cf(err);
        break;
    }
    case BIOS_OFF_DSKCHG:
        wr_ah(bios_dskchg(rd_al()));
        break;
    default:
        ESP_LOGW(TAG, "unknown BIOSSEG entry at IP=%04x — ignoring", ip);
        break;
    }
}
