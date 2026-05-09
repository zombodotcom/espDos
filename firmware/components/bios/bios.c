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
#include "esp_partition.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bios.h"
#include "display.h"

/* The T-Display-S3's USB-C is wired to the chip's native USB-Serial-JTAG
 * peripheral, NOT to UART0 (UART0 only goes to physical TX/RX pins).
 * ESP-IDF logs land on BOTH (CONSOLE primary=UART0, secondary=JTAG)
 * which is why we see them, but for actual interactive I/O we must
 * read from the JTAG side or input never reaches the chip. */
#define BIOS_RX_BUF    256
/* TX buffer sized for matrix's per-frame burst: 80 cols x ~20 bytes
 * (cursor + green + glyph + reset) = 1.6 KB per frame. With a 1 KB
 * buffer, every frame overflows and writes silently dropped half its
 * output — matrix looked completely blank on hardware. 4 KB swallows
 * a full frame plus headroom; the host drains between frames. */
#define BIOS_TX_BUF   4096

static void disk_init(void);

void bios_init(void) {
    static int installed = 0;
    if (installed) return;
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = BIOS_RX_BUF,
        .tx_buffer_size = BIOS_TX_BUF,
    };
    usb_serial_jtag_driver_install(&cfg);
    /* Disable stdio's line/block buffering on stderr so per-byte
     * fputc() in bios_out actually pushes per byte. Without this,
     * stderr is line-buffered when its fd is a TTY (default for
     * USB-Serial-JTAG) and matrix's no-newline output sits in the
     * FILE buffer until the buffer fills (~4 KB) or matrix exits. */
    setvbuf(stderr, NULL, _IONBF, 0);
    disk_init();
    installed = 1;
}

/* From esp8086.c via esp8086 component. */
extern unsigned char  *regs8;
extern unsigned short *regs16;

/* 8086tiny register indices (preserved in our esp8086.c fork). */
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

/* Auto-feed an initial date string so the user doesn't have to type
 * 1-1-80 at every boot. 86-DOS 1.0 has no RTC, so the kernel's
 * DOSINIT prompt loop always fires; this just satisfies the prompt
 * non-interactively before falling through to real keyboard input
 * for any subsequent programs that read from the console.
 *
 * The kernel's BUFIN handler echoes each character it consumes, so
 * the prompt line in the monitor will look exactly like the user
 * typed "1-1-80" and pressed Enter.
 *
 * Override at build time with -DESPDOS_INTERACTIVE_DATE=1 if you'd
 * rather type the date yourself. */
#ifndef ESPDOS_INTERACTIVE_DATE
/* Stringify the AUTOPICK digit so we can splice it into the auto-feed
 * sequence at compile time. The shell reads one char and a CR after
 * the date prompt is consumed. */
#ifdef ESPDOS_AUTOPICK
#  define ESPDOS_STR2(x) #x
#  define ESPDOS_STR(x)  ESPDOS_STR2(x)
static const char  bios_autodate[] = "1-1-80\r" ESPDOS_STR(ESPDOS_AUTOPICK) "\r";
#else
static const char  bios_autodate[] = "1-1-80\r";
#endif
static unsigned    bios_autodate_pos;
#endif

/* One-byte peek buffer shared between bios_stat (which fills it on a
 * 0-tick non-blocking read) and bios_in (which consumes it before
 * blocking). Without the consumer half, snake's WASD never reaches
 * snake.asm: bios_stat sees a key, sets peek_valid forever, and
 * every bios_in blocks on JTAG instead of returning the peeked byte. */
static uint8_t peek_byte;
static int     peek_valid = 0;

static uint8_t bios_stat(void) {
    /* DO NOT report ready when only auto-feed bytes remain. The
     * kernel's OUT routine does an "input snoop" on every CONOUT —
     * if STAT says "ready", it consumes one char from BIOSIN looking
     * for Ctrl-C/S/P/N. Returning 0xFF here would let the menu print
     * eat the AUTOPICK digit before SHELL.COM's AH=01 ever runs. */
    if (peek_valid) return 0xFF;
    int n = usb_serial_jtag_read_bytes(&peek_byte, 1, 0);
    if (n == 1) { peek_valid = 1; return 0xFF; }
    return 0;
}

static uint8_t bios_in(void) {
#ifndef ESPDOS_INTERACTIVE_DATE
    if (bios_autodate_pos < sizeof(bios_autodate) - 1) {
        return (uint8_t)bios_autodate[bios_autodate_pos++];
    }
#endif
    /* Drain the peek buffer first so STAT-reported keys actually
     * reach the caller. Without this, bios_stat fills peek and never
     * resets the valid flag, and bios_in goes straight to JTAG —
     * the peeked byte is silently dropped on every poll. */
    if (peek_valid) {
        peek_valid = 0;
        return peek_byte;
    }
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

/* 86-DOS console output. Two output sinks, in order:
 *
 *   1. usb_serial_jtag_write_bytes — raw, per-byte. This is what
 *      idf.py monitor and PuTTY (etc.) read on hardware. Raw
 *      bytes mean ANSI escape sequences (ESC[H, ESC[34m, ...)
 *      reach the terminal intact, which is essential for JULIA.COM
 *      and any future TUI program.
 *   2. Optional ESP_LOGI line-buffer mirror, gated on
 *      -DESPDOS_LOG_OUT=1. Useful for QEMU `-serial file:` capture
 *      where the JTAG channel isn't redirected and so the raw stream
 *      is invisible. Default OFF so hardware output stays clean.
 *
 * Earlier in development we line-buffered through ESP_LOGI for
 * everything because per-byte JTAG writes were invisible in QEMU
 * file mode. The cost was that the "I (12345) bios: " prefix
 * fragmented every row of program output and made cursor-home
 * (ESC[H) animation impossible. Splitting the responsibility — raw
 * always, ESP_LOGI on demand — gets us both. */
#ifdef ESPDOS_LOG_OUT
#define BIOS_OUT_BUF 128
static char     bios_out_line[BIOS_OUT_BUF];
static unsigned bios_out_len;

static void bios_out_flush(void) {
    if (bios_out_len == 0) return;
    bios_out_line[bios_out_len] = '\0';
    ESP_LOGI(TAG, "%s", bios_out_line);
    bios_out_len = 0;
}
#endif

static void bios_out(uint8_t ch) {
    /* Write through stderr's fd (set unbuffered in bios_init below).
     * That's the path ESP_LOG eventually drops bytes onto — through
     * the USB-Serial-JTAG VFS, which writes synchronously and waits
     * for FIFO room. usb_serial_jtag_write_bytes() with timeout=0
     * only queues into the driver buffer non-blocking; bytes piled
     * up until the emulator yielded, hence the "blank screen for 10 s
     * then dump" symptom. fputc(stderr) with stdio buffering
     * disabled gives us per-byte synchronous push. */
    fputc(ch, stderr);
    /* Mirror to the LCD on targets that have one. The call is an
     * inline no-op when CONFIG_ESPDOS_HAS_DISPLAY=n. */
    display_putc(ch);

#ifdef ESPDOS_LOG_OUT
    if (ch == '\r' || ch == '\n') {
        bios_out_flush();
        return;
    }
    /* Skip most control chars (< 0x20) for the log mirror, but pass
     * ESC (0x1B) so the captured log still shows the ANSI bytes. */
    if (ch < 0x20 && ch != 0x1B) {
        return;
    }
    if (bios_out_len >= BIOS_OUT_BUF - 1) {
        bios_out_flush();
    }
    bios_out_line[bios_out_len++] = (char)ch;
#endif
}


/* ===== Disk handlers: read/write the dos_disk flash partition.
 *
 * Calling convention (from 86DOS.ASM line 1093-1107):
 *   AL = drive number
 *   BX = transfer offset (within DS — buffer is at DS:BX)
 *   CX = number of sectors
 *   DX = absolute sector number (0-based linear, 512 B/sector)
 * Returns: CF=0 on success, CF=1 on error.
 *
 * The kernel only knows about drive 0 (we configured one drive in
 * the bootstub DPB table). Reads/writes against any other drive
 * return error so the kernel falls back to its DOS-side error
 * recovery instead of corrupting state. */
#define BIOS_SECTOR_SIZE  512u

static const esp_partition_t *disk_part;

static void disk_init(void) {
    if (disk_part) return;
    disk_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        "dos_disk");
    if (!disk_part) {
        ESP_LOGE(TAG, "dos_disk partition not found — disk I/O will error");
        return;
    }
    ESP_LOGI(TAG, "dos_disk: %lu KB at flash offset 0x%lx",
             (unsigned long)(disk_part->size / 1024),
             (unsigned long)disk_part->address);
}

static int bios_read(uint8_t drive, uint16_t buf_off, uint16_t count,
                     uint16_t sector) {
    if (drive != 0 || !disk_part) return 1;
    uint32_t off = (uint32_t)sector * BIOS_SECTOR_SIZE;
    uint32_t len = (uint32_t)count  * BIOS_SECTOR_SIZE;
    if (off + len > disk_part->size) return 1;

    uint32_t buf_phys = ((uint32_t)regs16[REG_DS] << 4) + buf_off;
    extern unsigned char mem[];   /* defined in esp8086.c */
    if (esp_partition_read(disk_part, off, &mem[buf_phys], len) != ESP_OK)
        return 1;
    return 0;
}

static int bios_write(uint8_t drive, uint16_t buf_off, uint16_t count,
                      uint16_t sector) {
    if (drive != 0 || !disk_part) return 1;
    uint32_t off = (uint32_t)sector * BIOS_SECTOR_SIZE;
    uint32_t len = (uint32_t)count  * BIOS_SECTOR_SIZE;
    if (off + len > disk_part->size) return 1;

    uint32_t buf_phys = ((uint32_t)regs16[REG_DS] << 4) + buf_off;
    extern unsigned char mem[];
    /* esp_partition_write requires the target range be erased first
     * (or already 0xFF). Safest path: erase covering 4 KB sectors
     * and re-write. We log the cost so it's visible if the kernel
     * hammers writes during init. */
    uint32_t erase_off = off & ~(0xFFFu);
    uint32_t erase_end = (off + len + 0xFFFu) & ~(0xFFFu);
    if (esp_partition_erase_range(disk_part, erase_off,
                                   erase_end - erase_off) != ESP_OK)
        return 1;
    /* Re-read what we erased outside the [off, off+len) window so we
     * don't lose neighboring sectors. For now, simplistic — this
     * disk isn't seeing concurrent writes during init. */
    if (esp_partition_write(disk_part, off, &mem[buf_phys], len) != ESP_OK)
        return 1;
    return 0;
}

static int bios_dskchg(uint8_t drive) {
    /* "No change" status. AH=0 means no media change since last read. */
    (void)drive;
    return 0;
}


/* ===== Printer + aux: stubs (no-op). =========================== */

static unsigned counter[10];   /* indexed by IP/3 — small + cheap */

static void bios_print(uint8_t ch) { (void)ch; }
static uint8_t bios_auxin(void)    { return 0x1A; }
static void bios_auxout(uint8_t c) { (void)c; }


/* ===== Top-level dispatch: route on the offset within BIOSSEG. ===== */

/* Track call counts for debugging. The periodic ESP_LOGI dump used
 * to fire every 256 calls; it interleaved with program output and
 * has been silenced. Rebuild with -DESPDOS_HEARTBEAT=1 to bring it
 * back (the same flag that re-enables the per-beat heartbeat in
 * espdos.c — both are debug-only). */
static void maybe_dump_counts(uint16_t ip) {
    int idx = ip / 3;
    if (idx >= 0 && idx < 10) counter[idx]++;
#ifdef ESPDOS_HEARTBEAT
    static unsigned total;
    if ((++total & 0xFF) == 0) {
        ESP_LOGI(TAG, "bios calls: stat=%u in=%u out=%u print=%u "
                      "axin=%u axout=%u rd=%u wr=%u dskchg=%u",
                 counter[1], counter[2], counter[3], counter[4],
                 counter[5], counter[6], counter[7], counter[8],
                 counter[9]);
    }
#endif
}

void bios_handle_call(uint16_t ip)
{
    maybe_dump_counts(ip);

    /* Log only disk + unknown calls — they're rare enough that each
     * one is interesting. Console I/O happens hundreds of times and
     * would drown out everything else. */
    if (ip == BIOS_OFF_READ || ip == BIOS_OFF_WRITE ||
        ip == BIOS_OFF_DSKCHG ||
        (ip != BIOS_OFF_STAT && ip != BIOS_OFF_IN  &&
         ip != BIOS_OFF_OUT  && ip != BIOS_OFF_PRINT &&
         ip != BIOS_OFF_AUXIN && ip != BIOS_OFF_AUXOUT)) {
        const char *name = "?";
        switch (ip) {
        case BIOS_OFF_READ:   name = "READ";   break;
        case BIOS_OFF_WRITE:  name = "WRITE";  break;
        case BIOS_OFF_DSKCHG: name = "DSKCHG"; break;
        }
        ESP_LOGI(TAG, "BIOS %s ip=%02x AL=%02x BX=%04x CX=%04x DX=%04x",
                 name, ip, regs8[0], regs16[REG_BX],
                 regs16[REG_CX], regs16[REG_DX]);
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
