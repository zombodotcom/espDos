; hello.asm — pipeline confidence harness for espDos.
;
; Loads at KERNEL_SEG:KERNEL_OFFSET — exactly where the real 86-DOS
; kernel goes — so the existing boot stub + emulator setup drives
; it identically. Exercises every BIOSSEG entry point our handlers
; care about, in a fixed sequence with deterministic output, so a
; host test can assert byte-exact correctness.
;
; Sequence:
;   1. BIOSOUT a banner string
;   2. BIOSIN one char, echo it
;   3. BIOSREAD sector 0 of drive 0 into a local buffer
;   4. BIOSOUT the first byte of that sector as 2 hex chars
;   5. JMP-loop forever (the test detects this as "done")
;
; If this runs end-to-end with the expected output, the pipeline
; (boot stub → KERNEL_SEG load → BIOSSEG far calls → register
; passing → DS:BX disk transfer → CF flag handling) is sound and
; any 86-DOS failures are kernel-side issues, not plumbing.

bits 16
org 0x100                  ; matches PUT 100H in 86DOS.ASM

BIOSSEG    equ 0x0040
BIOSIN     equ 0x06
BIOSOUT    equ 0x09
BIOSREAD   equ 0x15

%macro BIOS 1              ; CALL FAR BIOSSEG:%1
    call BIOSSEG:%1
%endmacro

start:
    push cs
    pop  ds                ; DS = our segment for string + buffer access

    mov  si, msg_hello
    call print

    BIOS BIOSIN            ; AL = typed char
    push ax
    mov  al, '>'           ; echo prefix
    call out_al
    pop  ax
    call out_al
    call crlf

    mov  si, msg_sector
    call print

    ; BIOSREAD: AL=drive, BX=offset, CX=count, DX=sector, DS=buf seg.
    ; We read sector 1 (FAT 1) rather than sector 0 (zeroed boot
    ; sector) because byte 0 of the FAT is the media descriptor —
    ; a non-zero value the host test can verify came from disk.
    mov  al, 0
    mov  bx, sector_buf
    mov  cx, 1
    mov  dx, 1
    BIOS BIOSREAD
    jc   disk_err

    mov  al, [sector_buf]
    call hex_byte
    call crlf

done:
    jmp  done              ; tight loop — host test sees IP wedged here

disk_err:
    mov  si, msg_dskerr
    call print
    jmp  done

; ---- Helpers ----

print:                     ; print zero-terminated string at DS:SI
    lodsb
    or   al, al
    jz   .ret
    call out_al
    jmp  print
.ret:
    ret

out_al:                    ; print AL via BIOSOUT, preserving BX
    push bx
    BIOS BIOSOUT
    pop  bx
    ret

crlf:
    mov  al, 0x0D
    call out_al
    mov  al, 0x0A
    call out_al
    ret

hex_byte:                  ; print AL as 2 uppercase hex chars
    push ax
    mov  ah, al
    shr  al, 4
    call hex_nib
    mov  al, ah
    call hex_nib
    pop  ax
    ret

hex_nib:                   ; print low nibble of AL
    and  al, 0x0F
    cmp  al, 10
    jb   .dig
    add  al, 'A' - 10 - '0'
.dig:
    add  al, '0'
    call out_al
    ret

; ---- Data ----

msg_hello:  db "espdos hello", 0x0D, 0x0A, "press any key: ", 0
msg_sector: db "fat[0]=", 0
msg_dskerr: db "(disk error)", 0x0D, 0x0A, 0

sector_buf: times 512 db 0
