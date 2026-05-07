; life.asm — Conway's Game of Life with color, for espDos.
;
; Loaded as LIFE.COM. Runs NUM_GENS generations of Conway's rules
; on a 78x24 toroidal grid, rendering each generation in a different
; ANSI color. Frames are separated by ESC[H (cursor home), so on a
; real terminal the previous generation is overwritten in place;
; on idf.py monitor (which strips ESC[H) the generations stack
; vertically and the user sees the evolution scroll past.
;
; Conway's rule (B3/S23):
;   - alive cell with 2 or 3 alive neighbors -> stays alive
;   - dead cell with exactly 3 alive neighbors -> born
;   - everything else -> dead
;
; Boundary: toroidal (edges wrap). Keeps a small grid more
; interesting — patterns that would otherwise drift off the edge
; come back from the other side.
;
; Memory layout (within USER_SEG, where the loader places this .COM):
;   0x0000..0x00FF   PSP (placeholder)
;   0x0100..code_end Code + small data
;   CUR_BUF=0x4000   1872 bytes — current generation, 1 byte per cell
;   NEXT_BUF=0x4800  1872 bytes — next generation
;   row_buf at end of code
;
; Buffers live OUTSIDE the .bin (we don't `db` 1872 zeros into the
; binary). Instead we declare equ constants and trust that the
; segment has 64 KB of memory and nothing else uses those addresses.
; This keeps the .COM file under 1 KB.

bits 16
cpu 8086
org 0x100

WIDTH       equ 78
HEIGHT      equ 24
CELL_COUNT  equ WIDTH * HEIGHT       ; 1872

NUM_GENS    equ 80                   ; total generations to run

; Buffers — addresses within DS (= USER_SEG after loader).
CUR_BUF     equ 0x4000               ; offset within DS
NEXT_BUF    equ 0x4800               ; immediately after CUR_BUF (1872 bytes)

; ---------- entry ----------
start:
    push    cs
    pop     ds
    push    cs
    pop     es

    call    seed_grid
    mov     word [gen_var], 0

gen_loop:
    ; Pick this generation's color: index = gen % 8.
    mov     ax, [gen_var]
    and     ax, 7
    mov     bx, ax
    shl     bx, 1                    ; bx = (gen % 8) * 2 (color index in table)
    mov     ax, [color_table + bx]
    mov     [color_var], ax          ; ASCII pair, e.g. '3' '4' for blue

    ; Cursor home so a real terminal overwrites the previous frame.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, esc_home
    int     0x21
    push    cs
    pop     ds

    call    render_grid
    call    step_grid
    call    swap_buffers

    inc     word [gen_var]
    mov     ax, [gen_var]
    cmp     ax, NUM_GENS
    jl      gen_loop

    ; Reset color and exit cleanly to SHELL.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, esc_reset
    int     0x21
    int     0x20

; ---------- seed_grid: zero buffers, plant the starting pattern ----------
seed_grid:
    push    cs
    pop     es
    mov     di, CUR_BUF
    mov     cx, CELL_COUNT * 2       ; zero both buffers in one rep stosb
    xor     al, al
    cld
    rep     stosb

    ; Plant patterns. Each entry in seed_pattern is (row, col); zero
    ; row terminates. R-pentomino at center, glider in upper-left,
    ; LWSS-type pattern on the right side, plus a small still life
    ; for visual contrast.
    mov     si, seed_pattern
.plant_loop:
    mov     al, [si]                 ; row
    or      al, al
    jz      .plant_done
    mov     ah, [si + 1]             ; col
    inc     si
    inc     si

    ; index = row * WIDTH + col   (WIDTH=78, fits in 16-bit)
    mov     bl, al                   ; bl = row
    xor     bh, bh                   ; bx = row
    mov     cx, WIDTH
    mov     ax, bx
    mul     cx                       ; AX = row * WIDTH
    mov     bl, ah                   ; restore col into bl (was in ah)
    xor     bh, bh                   ; clear high byte
    mov     bl, [si - 1]             ; bl = col (re-load)
    xor     bh, bh
    add     ax, bx                   ; AX = row*WIDTH + col
    mov     bx, ax
    mov     byte [bx + CUR_BUF], 1
    jmp     .plant_loop
.plant_done:
    ret

; ---------- step_grid: compute next generation in NEXT_BUF ----------
;
; For each cell at (row, col):
;   count = sum of 8 neighbors with toroidal wrap
;   if cur=1: alive iff (count==2 || count==3)
;   if cur=0: alive iff (count==3)
;
; Outer loop on row 0..HEIGHT-1, inner on col 0..WIDTH-1.
; Uses BP for cell-index pointer, BX for scratch. Slow but clear.
step_grid:
    mov     word [row_var], 0
.row:
    mov     word [col_var], 0
.col:
    ; Count alive neighbors.
    xor     bx, bx                   ; bx = neighbor count (0..8 fits in al)

    ; Iterate over 9 (drow, dcol) pairs; skip (0,0) which is the cell.
    mov     si, neighbor_offsets
    mov     cx, 8
.nbr:
    push    cx
    mov     al, [si]                 ; drow (-1, 0, +1 sign-extended)
    mov     ah, [si + 1]             ; dcol
    inc     si
    inc     si

    ; nrow = (row + drow + HEIGHT) mod HEIGHT
    mov     dl, [row_var]
    add     dl, al
    add     dl, HEIGHT
    cbw
    xor     dh, dh
    mov     cl, HEIGHT
    mov     al, dl
    xor     ah, ah
    div     cl                       ; AL = quotient, AH = remainder
    mov     dl, ah                   ; dl = nrow

    ; ncol = (col + dcol + WIDTH) mod WIDTH
    mov     bh, [col_var]
    add     bh, [si - 1]             ; + dcol  (already advanced si, so [si-1] is dcol)
    add     bh, WIDTH
    mov     al, bh
    xor     ah, ah
    mov     cl, WIDTH
    div     cl
    mov     bh, ah                   ; bh = ncol

    ; idx = nrow * WIDTH + ncol
    mov     al, dl                   ; al = nrow
    xor     ah, ah
    mov     cl, WIDTH
    mul     cl                       ; AX = nrow * WIDTH
    xor     dh, dh
    mov     dl, bh                   ; dx = ncol
    add     ax, dx
    mov     di, ax
    mov     al, [di + CUR_BUF]
    add     bl, al                   ; bl += alive (0 or 1)

    pop     cx
    loop    .nbr

    ; Get current cell value.
    mov     al, [row_var]
    xor     ah, ah
    mov     cl, WIDTH
    mul     cl
    mov     dl, [col_var]
    xor     dh, dh
    add     ax, dx
    mov     di, ax                   ; di = cell index
    mov     al, [di + CUR_BUF]       ; al = current state

    ; Apply rule.
    or      al, al
    jz      .dead_cell
    ; Alive: stays alive iff count==2 or count==3.
    cmp     bl, 2
    je      .next_alive
    cmp     bl, 3
    je      .next_alive
    jmp     .next_dead
.dead_cell:
    ; Dead: born iff count==3.
    cmp     bl, 3
    je      .next_alive
    jmp     .next_dead
.next_alive:
    mov     byte [di + NEXT_BUF], 1
    jmp     .advance
.next_dead:
    mov     byte [di + NEXT_BUF], 0
.advance:
    inc     word [col_var]
    mov     ax, [col_var]
    cmp     ax, WIDTH
    jl      .col
    inc     word [row_var]
    mov     ax, [row_var]
    cmp     ax, HEIGHT
    jl      .row
    ret

; ---------- swap_buffers: copy NEXT_BUF -> CUR_BUF ----------
swap_buffers:
    push    cs
    pop     es
    push    cs
    pop     ds
    mov     si, NEXT_BUF
    mov     di, CUR_BUF
    mov     cx, CELL_COUNT
    cld
    rep     movsb
    ret

; ---------- render_grid: emit one row at a time via AH=09 PRTBUF ----------
render_grid:
    mov     word [row_var], 0
.row:
    ; Build row buffer: WIDTH cells, alive=ESC[NNm# dead=' '.
    push    cs
    pop     ds
    mov     di, row_buf
    mov     word [col_var], 0

    ; Compute base cell index for this row: row_base = row * WIDTH.
    mov     ax, [row_var]
    mov     cl, WIDTH
    mul     cl                       ; AX = row * WIDTH
    mov     [row_base_var], ax

.cell:
    ; Look up cell.
    mov     bx, [row_base_var]
    add     bx, [col_var]
    mov     al, [bx + CUR_BUF]
    or      al, al
    jz      .dead

    ; Alive: write ESC '[' Nh Nl 'm' '#'
    mov     byte [di], 0x1B
    mov     byte [di + 1], '['
    mov     ax, [color_var]
    mov     [di + 2], ax             ; two-byte color code (al=Nh, ah=Nl)
    mov     byte [di + 4], 'm'
    mov     byte [di + 5], '#'
    add     di, 6
    jmp     .cell_done
.dead:
    ; Dead: just a space (no color reset; saves bytes).
    mov     byte [di], ' '
    inc     di
.cell_done:
    inc     word [col_var]
    mov     ax, [col_var]
    cmp     ax, WIDTH
    jl      .cell

    ; Append row terminator (CRLF + '$').
    mov     byte [di], 0x0D
    mov     byte [di + 1], 0x0A
    mov     byte [di + 2], '$'

    ; Emit the row.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, row_buf
    int     0x21

    inc     word [row_var]
    mov     ax, [row_var]
    cmp     ax, HEIGHT
    jl      .row
    ret

; ---------- data ----------
; Frame separator + reset.
esc_home:    db 0x1B,'[','H','$'
esc_reset:   db 0x1B,'[','0','m',0x0D,0x0A,'$'

; ANSI color codes (2-digit pairs, low byte = first digit, high byte = second).
; Picked for visibility on a black terminal.
color_table:
    db '3','4'                       ; blue
    db '3','6'                       ; cyan
    db '3','2'                       ; green
    db '3','3'                       ; yellow
    db '3','1'                       ; red
    db '3','5'                       ; magenta
    db '9','7'                       ; bright white
    db '9','3'                       ; bright yellow

; 8 (drow, dcol) neighbor offsets, skipping (0,0).
neighbor_offsets:
    db -1, -1
    db -1,  0
    db -1,  1
    db  0, -1
    db  0,  1
    db  1, -1
    db  1,  0
    db  1,  1

; Seed pattern: list of (row, col) cells to set alive, terminated by row=0.
; Note: row=0 is reserved as the terminator, so don't put cells there.
; R-pentomino at (10, 39):  ##  /##  / #
; Glider at (5, 5)
; LWSS at (15, 60)
seed_pattern:
    ; R-pentomino at center (rows 10-12, cols 38-40)
    db 10, 39
    db 10, 40
    db 11, 38
    db 11, 39
    db 12, 39
    ; Glider at upper-left (rows 5-7, cols 5-7)
    db 5, 6
    db 6, 7
    db 7, 5
    db 7, 6
    db 7, 7
    ; LWSS (lightweight spaceship) at (rows 15-18, cols 60-63)
    db 15, 61
    db 15, 64
    db 16, 60
    db 17, 60
    db 17, 64
    db 18, 60
    db 18, 61
    db 18, 62
    db 18, 63
    ; Block (still life) at (20, 30)
    db 20, 30
    db 20, 31
    db 21, 30
    db 21, 31
    ; Blinker (oscillator) at (3, 70)
    db 3, 69
    db 3, 70
    db 3, 71
    ; Terminator
    db 0, 0

; Per-frame state.
gen_var:        dw 0
row_var:        dw 0
col_var:        dw 0
row_base_var:   dw 0
color_var:      dw 0

; Row output buffer: WIDTH cells x max 6 bytes each + row_term (3) = 471 bytes.
row_buf:    times WIDTH*6 + 3 db 0
