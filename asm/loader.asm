; loader.asm — espDos transient loader.
;
; Runs immediately after FININIT's RETF returns into us (the bootstub
; pre-pushes a far pointer to this label so the kernel's RETF lands
; here). Loads HELLO.COM directly via BIOSREAD (the firmware-side
; trapped far call into BIOSSEG, same one the kernel uses) instead
; of going through INT 21h: (a) it avoids enabling interrupts, which
; the host emulator turns into spurious INT 0xA timer faults, and
; (b) the kernel's INT 21h SEQRD path divides by [BP+SECSIZ] and the
; DPB it computes for drive 0 in our minimal init walks somewhere
; with a 0 SECSIZ, so the divide instantly traps. Reading the disk
; raw is far simpler and still byte-for-byte correct: HELLO.COM
; lives entirely in cluster 2, which is sector 11 (0-based) of the
; FAT12 image we built in tools/build_disk.py. One BIOSREAD lifts it
; into the user segment.
;
; Assembled into the same segment as bootstub (EMU_BOOT_SEG = 0x0050)
; via INCBIN at LOADER_OFFSET in bootstub.bin. bootstub.asm pads up
; to LOADER_OFFSET before INCBIN'ing this blob, then pre-pushes a
; far pointer (cs, LOADER_OFFSET) onto the stack so the kernel's
; FININIT RETF naturally returns here.

bits 16
org LOADER_OFFSET                ; passed via -DLOADER_OFFSET=0x... at build

USER_SEG    equ 0x2000           ; arbitrary free segment for the TPA

; Sector layout (must match tools/build_disk.py):
;   sector  0          : boot sector (zeroed)
;   sectors 1..3       : FAT 1
;   sectors 4..6       : FAT 2 (mirror)
;   sectors 7..10      : root directory (4 sectors × 512 = 64 entries)
;   sectors 11..       : data area (cluster 2 starts here)
;
; LOAD_SECTOR / LOAD_COUNT can be overridden at assemble time so we
; can build loader variants that pull a different .COM off the disk
; (e.g. MANDEL.COM, which lives at a later cluster and is bigger
; than one sector).
%ifndef LOAD_SECTOR
  %define LOAD_SECTOR 11
%endif
%ifndef LOAD_COUNT
  %define LOAD_COUNT 1
%endif
HELLO_SECTOR equ LOAD_SECTOR

INT20_VEC   equ 0x20*4           ; physical offset of INT 20h vector

start:
    cli
    mov ax, cs
    mov ds, ax

    ; Reinstall IVT[20h] = (halt_ptr, our segment). The kernel
    ; overwrote our bootstub-installed vector during DOSINIT;
    ; restore it so HELLO's INT 20h returns to a known halt point.
    xor ax, ax
    mov es, ax
    mov word [es:INT20_VEC],     halt_ptr
    mov bx, cs
    mov word [es:INT20_VEC + 2], bx

    ; --- BIOSREAD: AL=drive, BX=offset, CX=count, DX=sector, DS=buf seg ---
    ; Load HELLO.COM's first sector into USER_SEG:0x100. The file is
    ; only 24 bytes so one sector covers it (HELLO.COM start lives
    ; at byte 0 of the sector; bytes 24..511 stay zero, harmless).
    mov ax, USER_SEG
    mov ds, ax                   ; DS = transfer buffer segment
    mov al, 0                    ; drive A:
    mov bx, 0x100                ; offset within USER_SEG
    mov cx, LOAD_COUNT           ; number of sectors to read
    mov dx, HELLO_SECTOR
    call 0x0040:0x0015           ; BIOSREAD trap entry (BIOSSEG:BIOSREAD)
    jc  fail

    ; All loaded. Standard COM-file launch: DS=ES=SS=USER_SEG, with a
    ; full-segment stack at the top of USER_SEG. The bootstub's 64-byte
    ; stack at 0050:00A6 is too shallow once a transient drives INT 21h
    ; in a tight loop (DOS 1.0's CONOUT path saves ~10 bytes per call;
    ; thousands of calls in MANDEL.COM's grid loop drain the stack into
    ; the bootstub's own code area, which clobbered MANDEL's col_var
    ; and made the col_loop terminate after one pixel).
    mov ax, USER_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE
    jmp word USER_SEG:0x100

fail:
    ; Any failure — emit '!' via BIOSOUT and fall into halt loop.
    mov al, '!'
    call 0x0040:0x0009           ; BIOSOUT
halt_ptr:                         ; same target as bootstub `halt`
    hlt
    jmp halt_ptr
