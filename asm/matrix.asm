; matrix.asm — MATRIX.COM. ANSI "matrix rain" effect.
;
; 80x24 playfield, 100 frames. Each column has an independent y-head
; tracked in `heads[]`; per frame we erase the trailing position,
; advance the head, and print a fresh random glyph in bright green at
; the new head. A 16-bit Galois LFSR drives both the initial scattered
; head positions and the per-frame glyph picks so the animation
; looks chaotic without burning bytes on tables.
;
; Output is via AH=09h (string) and AH=02h (char). Exits via INT 20h.
; The trailing reset_seq leaves the terminal in a sane color/cursor
; state when SHELL re-prompts.

bits 16
cpu 8086                         ; reject 286+ encodings (8086tiny only)
org 0x100

NUM_COLS    equ 80
NUM_ROWS    equ 24
TRAIL       equ 6                ; rows behind head to clear
FRAMES      equ 100

start:
    push cs
    pop  ds
    push cs
    pop  es

    ; Clear screen, hide cursor.
    mov  ah, 0x09
    mov  dx, init_seq
    int  0x21

    ; Scatter initial column heads to random rows in [0, NUM_ROWS).
    xor  bx, bx
.init_heads:
    cmp  bx, NUM_COLS
    jae  .init_done
    call lfsr
    and  ax, 0x1F                ; 0..31
    cmp  al, NUM_ROWS
    jb   .head_ok
    sub  al, NUM_ROWS
.head_ok:
    mov  [heads + bx], al
    inc  bx
    jmp  .init_heads
.init_done:

    ; Frame loop.
    mov  word [frame], 0
.frame_loop:
    cmp  word [frame], FRAMES
    jae  .done

    xor  bx, bx                  ; column index
.col_loop:
    cmp  bx, NUM_COLS
    jae  .next_frame

    ; Erase trail at (head[col] - TRAIL) mod NUM_ROWS, column bx.
    mov  al, [heads + bx]
    sub  al, TRAIL
    jns  .e_ok                   ; if non-negative, no wrap needed
    add  al, NUM_ROWS
.e_ok:
    mov  ah, al                  ; AH = row
    mov  al, bl                  ; AL = col (column always < 256)
    call cursor_to
    mov  ah, 0x02
    mov  dl, ' '
    int  0x21

    ; Advance head, wrap modulo NUM_ROWS.
    mov  al, [heads + bx]
    inc  al
    cmp  al, NUM_ROWS
    jb   .h_keep
    xor  al, al
.h_keep:
    mov  [heads + bx], al

    ; Print random green glyph at (head[col], col).
    mov  ah, [heads + bx]
    mov  al, bl
    call cursor_to
    mov  ah, 0x09
    mov  dx, green_seq
    int  0x21
    call lfsr
    and  al, 0x3F                ; pick from a 64-char range...
    add  al, '!'                 ; ...starting at '!' (printable)
    mov  dl, al
    mov  ah, 0x02
    int  0x21
    mov  ah, 0x09
    mov  dx, reset_seq
    int  0x21

    inc  bx
    jmp  .col_loop

.next_frame:
    inc  word [frame]
    jmp  .frame_loop

.done:
    ; Restore color, show cursor, move to last row, CRLF.
    mov  ah, 0x09
    mov  dx, exit_seq
    int  0x21
    int  0x20

; cursor_to — print "ESC[<row+1>;<col+1>H".
; Entry: AH = row (0..23), AL = col (0..79).
cursor_to:
    push ax
    push cx
    push dx
    push si
    mov  cl, ah
    inc  cl                      ; row+1 (1-based)
    inc  al                      ; col+1
    push ax                      ; save col+1 (low byte = col)
    ; Print "ESC["
    mov  dx, csi
    mov  ah, 0x09
    int  0x21
    ; Print row+1 as decimal (1..24, max 2 digits).
    mov  al, cl
    xor  ah, ah
    call print_dec_word
    ; Print ";"
    mov  ah, 0x02
    mov  dl, ';'
    int  0x21
    ; Print col+1 as decimal (1..80, max 2 digits).
    pop  ax
    xor  ah, ah
    call print_dec_word
    ; Print "H"
    mov  ah, 0x02
    mov  dl, 'H'
    int  0x21
    pop  si
    pop  dx
    pop  cx
    pop  ax
    ret

; print_dec_word — print AX (unsigned, ≤ 4 digits) as decimal.
print_dec_word:
    push ax
    push bx
    push cx
    push dx
    mov  bx, 10
    xor  cx, cx
.split:
    xor  dx, dx
    div  bx
    push dx
    inc  cx
    test ax, ax
    jnz  .split
.out:
    pop  dx
    add  dl, '0'
    mov  ah, 0x02
    int  0x21
    loop .out
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

; lfsr — 16-bit Galois LFSR PRNG. Returns next value in AX.
lfsr:
    mov  ax, [seed]
    shr  ax, 1
    jnc  .no_xor
    xor  ax, 0xB400
.no_xor:
    test ax, ax
    jnz  .save
    inc  ax                      ; never let seed become zero
.save:
    mov  [seed], ax
    ret

; Data
seed:       dw 0xACE1
frame:      dw 0
csi:        db 0x1B, '[', '$'
green_seq:  db 0x1B, '[1;32m', '$'              ; bright green
reset_seq:  db 0x1B, '[0m', '$'
init_seq:   db 0x1B, '[2J', 0x1B, '[H', 0x1B, '[?25l', '$'   ; clear + home + hide cursor
exit_seq:   db 0x1B, '[0m', 0x1B, '[?25h', 0x1B, '[24;1H', 0x0D, 0x0A, '$'  ; reset + show cursor + last row
heads:      times NUM_COLS db 0
