; bootstub.asm — minimal 8086 bootstrap for espDos.
;
; Loaded by the firmware at EMU_BOOT_SEG:EMU_BOOT_OFFSET (see
; firmware/components/esp8086/include/esp8086.h). Initial CS:IP is
; set to that same address, so this is the very first 8086 code that
; runs inside our emulator.
;
; Job: set up the IVT entries the kernel assumes are already valid
; before any program terminates / hits Ctrl-C / raises a critical
; error, install a real stack with a far pointer to our transient
; loader pre-pushed on it, then JMP FAR to the kernel.
;
; The pre-pushed pointer is what 86-DOS's FININIT routine returns to
; via its `RET L` (RETF) at the end of init: with no boot-loader-
; supplied stack the kernel ends up returning into IVT[0] (DIVOV) by
; accident, so its design implicitly relies on the boot stub leaving
; the program-entry far pointer at SS:SP. (Verified empirically by
; tests/emu/test_fininit_stack.c.)
;
; Build:
;   nasm -f bin -DKERNEL_SEG=0x0100 -DKERNEL_OFFSET=0x0100 \
;        -DLOADER_OFFSET=0x80 \
;        -o build/bootstub.bin asm/bootstub.asm
; The KERNEL_SEG / KERNEL_OFFSET / LOADER_OFFSET defines come from
; the build script — single source of truth.

bits 16
org 0

%ifndef KERNEL_SEG
  %error "KERNEL_SEG not defined — pass via nasm -DKERNEL_SEG=0x..."
%endif
%ifndef KERNEL_OFFSET
  %error "KERNEL_OFFSET not defined — pass via nasm -DKERNEL_OFFSET=0x..."
%endif
%ifndef LOADER_OFFSET
  %error "LOADER_OFFSET not defined — pass via nasm -DLOADER_OFFSET=0x..."
%endif

start:
    cli

    ; DS = 0 so we can write to the IVT, which lives at 0:0000.
    xor ax, ax
    mov ds, ax

    ; Install our `halt` label as the target for the exit-class
    ; vectors. Each IVT slot is 4 bytes: offset (word), then
    ; segment (word). Slot N lives at linear address N*4.
    ;
    ;   20h  program terminate
    ;   22h  exit address (where to go on program end)
    ;   23h  Ctrl-C handler
    ;   24h  critical error handler
    ;   27h  terminate-and-stay-resident return
    mov bx, halt           ; offset of halt within our segment
    mov ax, cs             ; our segment (= EMU_BOOT_SEG at runtime)

    mov word [20h*4],     bx
    mov word [20h*4 + 2], ax
    mov word [22h*4],     bx
    mov word [22h*4 + 2], ax
    mov word [23h*4],     bx
    mov word [23h*4 + 2], ax
    mov word [24h*4],     bx
    mov word [24h*4 + 2], ax
    mov word [27h*4],     bx
    mov word [27h*4 + 2], ax

    ; Install IRET handler for the hardware interrupts our emulator
    ; raises once the kernel does an STI (it sets FLAG_IF and then
    ; pc_interrupt(0xA) fires from the host-side timer poll). Without
    ; a stub here, IVT[8] / IVT[0xA] read 0:0 and the emu halts.
    mov bx, iret_stub
    mov word [8h*4],     bx
    mov word [8h*4 + 2], ax
    mov word [0Ah*4],     bx
    mov word [0Ah*4 + 2], ax

    ; --- Install a real stack and pre-push a far pointer to the
    ; transient loader (which is INCBIN'd at LOADER_OFFSET in this
    ; same segment). FININIT's RETF will pop it as its return target.
    mov ax, cs
    mov ss, ax
    mov sp, stack_top
    push ax                ; CS for the far return
    ; NB: `push imm16` is 80186+; the 8086 has no such opcode, and our
    ; emulator (8086tiny lineage) doesn't decode 0x68. Materialise the
    ; immediate via BX and `push bx` instead.
    mov bx, LOADER_OFFSET
    push bx                ; IP for the far return — loader entry

    ; Set DS:SI to point at our DPB init table. 86-DOS's DOSINIT
    ; does `LODB` (read byte from DS:[SI++] into AL) on its very
    ; first instruction (line 3301 of 86DOS.ASM) to get NUMDRV,
    ; then loops for each drive reading a DPT pointer. If we don't
    ; set DS:SI here, the kernel reads zero from low memory, ends
    ; up with NUMDRV=0 and no drives configured — which silently
    ; corrupts later disk and exit-vector state.
    mov ds, ax
    mov si, dpb_table

    sti

    ; JMP FAR to the kernel. NASM encodes this as opcode 0xEA
    ; (JMP FAR ptr16:16) with the offset and segment from the
    ; build-time -D defines.
    jmp word KERNEL_SEG:KERNEL_OFFSET

; Safe-halt landing pad for any vector that ends up here.
; HLT halts until an interrupt; the JMP is a belt-and-suspenders
; tight loop in case interrupts wake us up. The firmware can
; observe CS:IP frozen on these two bytes and know "kernel exited
; cleanly via boot-stub halt" rather than "wandered off into junk".
halt:
    hlt
    jmp halt

; Tiny IRET stub for the IVT[8] / IVT[0xA] timer-tick vectors (see
; comment in `start:`). The host emulator pushes FLAGS, CS, IP via
; pc_interrupt; an unconditional IRET unwinds them back to caller.
iret_stub:
    iret

; ===== DPB init table =====
; Format derived from PERDRV in 86DOS.ASM lines 3306-3389. The
; kernel reads this with DS:SI pointing here:
;
;   byte  NUMDRV              count of drives that follow
;   word  DPT_PTR_0           offset of drive 0's DPT (in DS)
;   word  DPT_PTR_N...        repeat NUMDRV times
;
; Each DPT pointed to has:
;
;   word  SECSIZ              sector size in bytes (512)
;   byte  SEC_PER_CLUS        sectors per cluster (1)
;   word  FIRFAT              # reserved sectors before FAT (1)
;   byte  FATCNT              # of FATs (2)
;   word  MAXENT              max root-dir entries (64)
;   word  DSKSIZ              total sectors on disk (720 = 360 KB)
;
; Geometry must match build/disk.img produced by tools/build_disk.py.
dpb_table:
    db 1                     ; NUMDRV — one drive (A:)
    dw dpt_drive_a           ; DPT_PTR for drive 0

dpt_drive_a:
    dw 512                   ; SECSIZ
    db 1                     ; SEC_PER_CLUS
    dw 1                     ; FIRFAT
    db 2                     ; FATCNT
    dw 64                    ; MAXENT
    dw 720                   ; DSKSIZ

; ===== Stack region =====
; 64 bytes ought to cover anything DOSINIT does before FININIT — the
; kernel only does a handful of pushes on this path (verified by the
; trace dumped by test_fininit_stack.c).
            times 64 db 0
stack_top:

; ===== Loader image =====
; Pad to LOADER_OFFSET, then INCBIN the assembled loader.bin. NASM
; has no `times` form that pads to an absolute address; use the
; `$ - $$` trick (offset within section) and pad accordingly.
            times (LOADER_OFFSET - ($ - $$)) db 0
loader_image:
            incbin "build/loader.bin"
