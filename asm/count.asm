; count.asm — COUNT.COM transient for espDos.
;
; Counts 1..50 printing each number as two-digit decimal with a
; space separator, 10 numbers per line. Exercises integer division
; (AAM) and INT 21h AH=02h CONOUT — useful as a third demo program
; alongside HELLO.COM (string output) and MANDEL.COM (heavy IMUL).
;
; Roughly 80 bytes, well under one sector.

bits 16
cpu 8086                   ; reject 286+ encodings (8086tiny only)
org 0x100

start:
    mov  cx, 1                 ; current number (1..MAX)

next:
    mov  ax, cx                ; AL = number to print (fits in byte: 1..50)
    aam                        ; AH = AL / 10, AL = AL % 10
    add  ax, 0x3030            ; ASCII-ify both nibbles
    push cx
    mov  dl, ah
    mov  ah, 0x02              ; INT 21h AH=02h CONOUT
    int  0x21
    pop  cx
    push cx
    mov  ax, cx
    aam
    add  ax, 0x3030
    mov  dl, al
    mov  ah, 0x02
    int  0x21

    ; Decide separator: newline every 10th number, space otherwise.
    pop  cx
    mov  ax, cx
    mov  bl, 10
    div  bl                    ; AH = CX % 10
    or   ah, ah
    jz   newline

    mov  dl, ' '
    mov  ah, 0x02
    int  0x21
    jmp  step

newline:
    mov  dl, 0x0D
    mov  ah, 0x02
    int  0x21
    mov  dl, 0x0A
    mov  ah, 0x02
    int  0x21

step:
    inc  cx
    cmp  cx, 51                ; print 1..50 inclusive
    jne  next

    int  0x20
