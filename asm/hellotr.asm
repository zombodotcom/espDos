; hellotr.asm — HELLO.COM transient for espDos.
;
; A real "hello world" running through Tim Paterson's 86-DOS 1.00.
; Loaded off the FAT12 disk by asm/loader.asm at USER_SEG:0x100, then
; printed via INT 21h AH=09h ($-terminated string). INT 20h returns
; control to the bootstub halt loop.
;
; Origin 100h is standard .COM convention — DOS loads the file at
; PSP:0x100 with the PSP filling 0x00..0xFF.

bits 16
cpu 8086                   ; reject 286+ encodings (8086tiny only)
org 0x100

start:
    mov  ah, 0x09
    mov  dx, msg
    int  0x21
    int  0x20

msg:
    db 0x0D, 0x0A
    db '+----------------------------------------+', 0x0D, 0x0A
    db '|  Hello, World!                         |', 0x0D, 0x0A
    db '|  This is HELLO.COM running on espDos:  |', 0x0D, 0x0A
    db '|  Tim Paterson 86-DOS 1.00, on ESP32-S3 |', 0x0D, 0x0A
    db '+----------------------------------------+', 0x0D, 0x0A
    db '$'
