#pragma once

#include <stdint.h>

/*
 * bios — handlers invoked when 86-DOS calls into BIOSSEG (0x0040).
 *
 * Background: 86DOS.ASM expects the IBM-PC-style BIOS to live at
 * segment 0x0040, with each function as a 3-byte JMP-far stub at a
 * fixed offset. The original kernel makes calls like
 *     CALL BIOSREAD, BIOSSEG
 * which is `CALL FAR 0x0040:0x0015`. In a real PC, that landed in
 * IO.SYS / IBMBIO.COM. In our world, the firmware traps when the
 * emulator's CS becomes 0x0040 and dispatches based on the offset.
 *
 * After the handler runs, the emulator simulates RETF (pops 4 bytes
 * for return CS:IP) and resumes in kernel code.
 *
 * BIOS entry-point offsets per 86DOS.ASM lines 130-143:
 *
 *   0x03  BIOSSTAT     console status:    AL=0 no char ready, AL!=0 ready
 *   0x06  BIOSIN       console input:     return AL = char
 *   0x09  BIOSOUT      console output:    AL = char to write
 *   0x0C  BIOSPRINT    printer output:    AL = char
 *   0x0F  BIOSAUXIN    serial input:      return AL = char
 *   0x12  BIOSAUXOUT   serial output:     AL = char
 *   0x15  BIOSREAD     disk read:         AL=drv, BX=buf, CX=count, DX=sector
 *   0x18  BIOSWRITE    disk write:        same args
 *   0x1B  BIOSDSKCHG   disk change:       AL=drv, return AH
 */

#define BIOS_OFF_STAT     0x03
#define BIOS_OFF_IN       0x06
#define BIOS_OFF_OUT      0x09
#define BIOS_OFF_PRINT    0x0C
#define BIOS_OFF_AUXIN    0x0F
#define BIOS_OFF_AUXOUT   0x12
#define BIOS_OFF_READ     0x15
#define BIOS_OFF_WRITE    0x18
#define BIOS_OFF_DSKCHG   0x1B

/*
 * Called from emu_run_n() when CS == EMU_BIOS_SEG. `ip` is the
 * offset within BIOSSEG that the kernel called. The handler reads
 * args from the emulator's registers (AX/BX/CX/DX/DS/ES/...) and
 * writes results back via the same.
 */
void bios_handle_call(uint16_t ip);

/*
 * Install the UART driver so bios_in/bios_out can use uart_read_bytes
 * / uart_write_bytes. Must be called from app_main before driving
 * the emulator. Idempotent.
 */
void bios_init(void);
