; snake.asm — SNAKE.COM. Playable Snake in 80x24, keyboard-driven.
;
; This is the demo that proves "keypress during a running program"
; actually works through the kernel's BIOSIN/STATCHK path. Real DOS
; programs rely on AH=06h DL=0xFF (RAWIO conditional input) to read
; the next pending byte without blocking; we do the same here, which
; means a fix to the SHELL's input-snoop bypass also has to make
; per-frame polling work for SNAKE.
;
; Controls: W/A/S/D for up/left/down/right; Q to quit. Anti-reverse
; check prevents instantly losing by hitting "back into yourself."
; Game ends on wall hit, self hit, or Q. After end the program
; restores cursor + colors and INT 20h's back to SHELL.
;
; Body is a fixed-length circular array of (row, col) bytes. We grow
; on food eat (FOOD_GROW) but cap at MAX_LEN; any further food just
; resets the food without lengthening.

bits 16
cpu 8086                         ; reject 286+ encodings (8086tiny only)
org 0x100

NUM_COLS    equ 80
NUM_ROWS    equ 24
START_LEN   equ 5
MAX_LEN     equ 60               ; body array size (each cell = row+col)
FOOD_GROW   equ 1                ; cells added per food eaten
POLL_TICKS  equ 8000             ; spin-poll iterations per frame ≈ frame delay

start:
    push cs
    pop  ds
    push cs
    pop  es

    ; Clear screen, hide cursor, draw border row of '+' along edges.
    mov  dx, init_seq
    call print_str

    ; Place initial snake horizontally at row 12, head at col 44,
    ; tail at col 40 (so direction = right means heading off into open
    ; playfield).
    mov  word [length], START_LEN
    mov  byte [direction], 1     ; 1 = right
    xor  bx, bx                  ; bx = body index (head-first)
.init_body:
    cmp  bx, START_LEN
    jae  .init_done
    mov  ax, bx
    shl  ax, 1                   ; byte offset = i * 2
    mov  si, ax
    mov  byte [body + si + 0], 12
    mov  ax, START_LEN - 1
    sub  ax, bx                  ; ax = (START_LEN-1) - i
    add  ax, 40                  ; col = 40 + (START_LEN-1) - i
    mov  [body + si + 1], al
    inc  bx
    jmp  .init_body
.init_done:

    ; Draw initial snake.
    mov  bx, 0
.draw_init:
    cmp  bx, [length]
    jae  .place_food
    push bx
    mov  ax, bx
    shl  ax, 1
    mov  si, ax
    mov  ah, [body + si + 0]
    mov  al, [body + si + 1]
    call cursor_to
    mov  dx, snake_glyph
    call print_str
    pop  bx
    inc  bx
    jmp  .draw_init

.place_food:
    call new_food

.frame_loop:
    ; Poll for a key (with delay). Returns AL=key (or 0 if timeout).
    call poll_key

    ; Update direction based on the (possibly zero) AL.
    test al, al
    jz   .no_input
    or   al, 0x20                ; force lowercase for compare
    cmp  al, 'q'
    je   .quit
    cmp  al, 'w'
    jne  .ck_a
    cmp  byte [direction], 2     ; can't reverse from down
    je   .no_input
    mov  byte [direction], 0
    jmp  .no_input
.ck_a:
    cmp  al, 'a'
    jne  .ck_s
    cmp  byte [direction], 1
    je   .no_input
    mov  byte [direction], 3
    jmp  .no_input
.ck_s:
    cmp  al, 's'
    jne  .ck_d
    cmp  byte [direction], 0
    je   .no_input
    mov  byte [direction], 2
    jmp  .no_input
.ck_d:
    cmp  al, 'd'
    jne  .no_input
    cmp  byte [direction], 3
    je   .no_input
    mov  byte [direction], 1
.no_input:

    ; Compute new head from body[0] + direction-delta.
    mov  ah, [body + 0]          ; current head row
    mov  al, [body + 1]          ; current head col
    mov  dl, [direction]
    cmp  dl, 0
    jne  .nd0
    dec  ah
    jmp  .have_head
.nd0:
    cmp  dl, 1
    jne  .nd1
    inc  al
    jmp  .have_head
.nd1:
    cmp  dl, 2
    jne  .nd2
    inc  ah
    jmp  .have_head
.nd2:
    dec  al
.have_head:
    ; Wall check.
    cmp  ah, NUM_ROWS
    jae  .gameover
    cmp  al, NUM_COLS
    jae  .gameover

    ; Self check: walk body, look for (ah, al) match.
    mov  bx, 0
.self_loop:
    cmp  bx, [length]
    jae  .self_ok
    push bx
    shl  bx, 1
    mov  dh, [body + bx + 0]
    mov  dl, [body + bx + 1]
    pop  bx
    cmp  dh, ah
    jne  .self_next
    cmp  dl, al
    jne  .self_next
    jmp  .gameover
.self_next:
    inc  bx
    jmp  .self_loop
.self_ok:

    ; Did we eat food?
    push ax                      ; save (head_row, head_col)
    mov  dh, [food_row]
    mov  dl, [food_col]
    cmp  ah, dh
    jne  .no_eat
    cmp  al, dl
    jne  .no_eat
    ; Ate food: grow length by FOOD_GROW (capped).
    mov  ax, [length]
    add  ax, FOOD_GROW
    cmp  ax, MAX_LEN
    jbe  .grow_ok
    mov  ax, MAX_LEN
.grow_ok:
    mov  [length], ax
    pop  ax                      ; restore head row/col
    push ax
    call shift_body              ; insert new head, tail stays (snake grew)
    pop  ax
    call new_food
    jmp  .draw_head

.no_eat:
    pop  ax                      ; restore head
    push ax
    ; Erase the current tail position.
    mov  bx, [length]
    dec  bx
    shl  bx, 1
    mov  ah, [body + bx + 0]
    mov  al, [body + bx + 1]
    call cursor_to
    mov  ah, 0x06
    mov  dl, ' '
    int  0x21
    pop  ax
    push ax
    call shift_body
    pop  ax

.draw_head:
    ; Render the new head.
    call cursor_to
    mov  dx, snake_glyph
    call print_str

    jmp  .frame_loop

.gameover:
    mov  dx, gameover_seq
    call print_str
.quit:
    mov  dx, exit_seq
    call print_str
    int  0x20

; -------------------------------------------------------------------
; shift_body — body[i] = body[i-1] for i = length-1 down to 1; then
; body[0] = (AH, AL). Effectively pushes a new head onto a fixed-length
; queue. Caller passes new head row in AH, col in AL.
shift_body:
    push ax
    push bx
    push cx
    push dx
    mov  cx, [length]
    dec  cx                      ; cx = length-1 (number of slots to shift)
    test cx, cx
    jz   .write_head
    mov  bx, cx
    shl  bx, 1                   ; byte offset of last cell
.shift_one:
    mov  dh, [body + bx - 2]
    mov  dl, [body + bx - 1]
    mov  [body + bx + 0], dh
    mov  [body + bx + 1], dl
    sub  bx, 2
    loop .shift_one
.write_head:
    mov  [body + 0], ah
    mov  [body + 1], al
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

; -------------------------------------------------------------------
; new_food — pick a random cell, store in (food_row, food_col), draw
; an apple ('@') there. Doesn't bother to ensure the cell isn't on
; the snake — collisions just look like a free meal next frame.
new_food:
    push ax
    push bx
    push cx
    push dx
.try:
    call lfsr
    mov  cx, ax
    and  cx, 0x1F                ; row 0..31, retry if >= 24
    cmp  cx, NUM_ROWS
    jae  .try
    mov  [food_row], cl
    call lfsr                    ; fresh random for the column
    mov  bx, NUM_COLS
    xor  dx, dx
    div  bx                      ; AX = lfsr_out / 80, DX = % 80
    mov  [food_col], dl
    mov  ah, [food_row]
    mov  al, [food_col]
    call cursor_to
    mov  dx, food_seq
    call print_str
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

; -------------------------------------------------------------------
; poll_key — spin POLL_TICKS times calling AH=06h DL=0xFF. Returns
; AL = the *last* key pressed during the spin (or 0 if none).
poll_key:
    push bx
    push cx
    push dx
    xor  bx, bx                  ; BL = last key seen (0 = none)
    mov  cx, POLL_TICKS
.spin:
    mov  ah, 0x06
    mov  dl, 0xFF
    int  0x21
    jz   .next                   ; no key pending
    mov  bl, al
.next:
    loop .spin
    mov  al, bl
    pop  dx
    pop  cx
    pop  bx
    ret

; -------------------------------------------------------------------
; cursor_to — print "ESC[<row+1>;<col+1>H".
; Entry: AH = row (0..23), AL = col (0..79). Preserves all regs.
cursor_to:
    push ax
    push cx
    push dx
    mov  cl, ah
    inc  cl                      ; row+1
    inc  al                      ; col+1
    push ax
    mov  dx, csi
    call print_str
    mov  al, cl
    xor  ah, ah
    call print_dec_word
    mov  ah, 0x06
    mov  dl, ';'
    int  0x21
    pop  ax
    xor  ah, ah
    call print_dec_word
    mov  ah, 0x06
    mov  dl, 'H'
    int  0x21
    pop  dx
    pop  cx
    pop  ax
    ret

; -------------------------------------------------------------------
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
    mov  ah, 0x06
    int  0x21
    loop .out
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

; -------------------------------------------------------------------
; print_str — print '$'-terminated string at DS:DX, byte-by-byte via
; AH=06h (RAWOUT). Equivalent of AH=09h PRTBUF but bypasses the
; kernel's STATCHK→INCHK snoop, which would otherwise eat keystrokes
; the player meant for poll_key during every frame's render burst.
; That's exactly the bug that made WASD silently fail. Preserves all
; registers except FLAGS.
print_str:
    push ax
    push dx
    push si
    mov  si, dx
.loop:
    mov  dl, [si]
    cmp  dl, '$'
    je   .done
    mov  ah, 0x06
    int  0x21
    inc  si
    jmp  .loop
.done:
    pop  si
    pop  dx
    pop  ax
    ret

; -------------------------------------------------------------------
; lfsr — 16-bit Galois LFSR. Returns next value in AX.
lfsr:
    mov  ax, [seed]
    shr  ax, 1
    jnc  .no_xor
    xor  ax, 0xB400
.no_xor:
    test ax, ax
    jnz  .save
    inc  ax
.save:
    mov  [seed], ax
    ret

; -------------------------------------------------------------------
; Data
length:        dw 0
direction:     db 0              ; 0=up 1=right 2=down 3=left
food_row:      db 0
food_col:      db 0
seed:          dw 0xACE1

csi:           db 0x1B, '[', '$'
init_seq:      db 0x1B, '[2J', 0x1B, '[H', 0x1B, '[?25l', 0x1B, '[1;32m', '$'
exit_seq:      db 0x1B, '[0m', 0x1B, '[?25h', 0x1B, '[24;1H', 0x0D, 0x0A, '$'
gameover_seq:  db 0x1B, '[1;31m', 0x1B, '[12;33H', 'GAME OVER', '$'
snake_glyph:   db 0x1B, '[1;32m', 'O', '$'
food_seq:      db 0x1B, '[1;33m', '@', '$'

body:          times MAX_LEN * 2 db 0
