; life.asm — Conway's Game of Life with age-aware color, for espDos.
;
; Loaded as LIFE.COM. Runs NUM_GENS generations of Conway's rules
; on a 78x24 toroidal grid. Each cell carries one of four states
; (long-dead / just-died / long-alive / newborn) so render can
; show fading dying cells in gray, ignition cells in bright white,
; and the established population in a per-generation cycling color.
;
; Conway's rule (B3/S23):
;   - alive cell with 2 or 3 alive neighbors -> stays alive
;   - dead cell with exactly 3 alive neighbors -> born
;   - everything else -> dead
;
; Cell-state encoding (one byte per cell, in CUR_BUF / NEXT_BUF):
;
;   0 = long-dead    (renders as ' ')
;   1 = just-died    (renders as ESC[90m. — dim gray, low-density)
;   2 = long-alive   (renders as ESC[Nm# — gen-cycling color)
;   3 = newborn      (renders as ESC[97m# — bright white, ignition)
;
; Convenient property of this encoding: low bit of cell value is
; 0 for "alive long enough that it counts as a parent" — actually
; *high* bit (>>1) is what we need: 0,1 -> 0 (dead); 2,3 -> 1 (alive).
; So neighbor-counting becomes "shr al, 1; add bl, al" — adds a single
; instruction but keeps the same outer loop shape.
;
; Boundary: toroidal (edges wrap). Seed: 16-bit LCG fills the grid
; at ~25% density. The same fixed seed gives a reproducible run; the
; visual interest comes from how Conway's rules shake the chaos out
; over the next 200 generations rather than from random restart.
;
; Memory layout (within CHILD_SEG, where SHELL placed this .COM):
;   0x0000..0x00FF   PSP placeholder
;   0x0100..code_end Code + small data + row_buf
;   0x4000           CUR_BUF  (1872 bytes)
;   0x4800           NEXT_BUF (1872 bytes)
;
; Buffers live OUTSIDE the .bin (no `db` of zeros into the binary).
; CHILD_SEG owns 64 KB of segment memory; nothing else uses these
; offsets. Keeps the .COM file under ~1.5 KB.

bits 16
cpu 8086
org 0x100

WIDTH       equ 78
HEIGHT      equ 24
CELL_COUNT  equ WIDTH * HEIGHT       ; 1872

NUM_GENS    equ 200                  ; total generations to run

; Buffers — addresses within DS (= CHILD_SEG after loader).
CUR_BUF     equ 0x4000
NEXT_BUF    equ 0x4800

; ---------- entry ----------
start:
    push    cs
    pop     ds
    push    cs
    pop     es

    call    seed_grid
    mov     word [gen_var], 0

gen_loop:
    ; Pick this generation's "long-alive" color: index = gen % 8.
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

; ---------- seed_grid: fill CUR_BUF with ~25% density via LCG ----------
;
; LCG: seed_{n+1} = seed_n * 25173 + 13849, period 2^16. Cell alive
; iff low two bits of the random word == 0 (1-in-4 chance, ~25%).
; Alive cells start in state 2 (long-alive) so render colors them
; with the per-generation hue rather than ignition white.
seed_grid:
    push    cs
    pop     es
    mov     di, NEXT_BUF
    mov     cx, CELL_COUNT * 2       ; zero CUR_BUF + NEXT_BUF region
    xor     al, al
    cld
    rep     stosb

    mov     word [rng_seed_var], 0xCAFE
    push    cs
    pop     es
    mov     di, CUR_BUF
    mov     cx, CELL_COUNT
.fill:
    push    cx
    mov     ax, [rng_seed_var]
    mov     bx, 25173
    mul     bx                       ; DX:AX = ax * 25173
    add     ax, 13849
    mov     [rng_seed_var], ax

    ; LCG low bits are non-random (period 4 on bits 0-1 for this
     ; multiplier+increment), so test the *high* byte instead.
     ; Alive iff (ah & 0x03) == 0 → ~25% density on uniform ah.
    test    ah, 0x03
    jnz     .dead
    mov     al, 2                    ; long-alive
    jmp     .write
.dead:
    mov     al, 0                    ; long-dead
.write:
    mov     [di], al
    inc     di
    pop     cx
    loop    .fill
    ret

; ---------- step_grid: compute next generation in NEXT_BUF ----------
;
; For each cell at (row, col):
;   count = sum of (cell >> 1) over 8 neighbors with toroidal wrap
;     (states 0,1 contribute 0, states 2,3 contribute 1)
;   old_alive = (self >> 1) != 0
;   new_alive = (count==3) || (count==2 && old_alive)
;   next state = 0/1/2/3 by transition
step_grid:
    mov     word [row_var], 0
.row:
    mov     word [col_var], 0
.col:
    xor     bx, bx                   ; bx = neighbor count

    mov     si, neighbor_offsets
    mov     cx, 8
.nbr:
    push    cx
    mov     al, [si]                 ; drow
    mov     ah, [si + 1]             ; dcol
    inc     si
    inc     si

    ; nrow = (row + drow + HEIGHT) mod HEIGHT
    mov     dl, [row_var]
    add     dl, al
    add     dl, HEIGHT
    xor     dh, dh
    mov     cl, HEIGHT
    mov     al, dl
    xor     ah, ah
    div     cl
    mov     dl, ah                   ; dl = nrow

    ; ncol = (col + dcol + WIDTH) mod WIDTH
    mov     bh, [col_var]
    add     bh, [si - 1]
    add     bh, WIDTH
    mov     al, bh
    xor     ah, ah
    mov     cl, WIDTH
    div     cl
    mov     bh, ah                   ; bh = ncol

    ; idx = nrow * WIDTH + ncol; alive contribution = cell >> 1
    mov     al, dl
    xor     ah, ah
    mov     cl, WIDTH
    mul     cl
    xor     dh, dh
    mov     dl, bh
    add     ax, dx
    mov     di, ax
    mov     al, [di + CUR_BUF]
    shr     al, 1                    ; 0,1 -> 0; 2,3 -> 1
    add     bl, al

    pop     cx
    loop    .nbr

    ; self index
    mov     al, [row_var]
    xor     ah, ah
    mov     cl, WIDTH
    mul     cl
    mov     dl, [col_var]
    xor     dh, dh
    add     ax, dx
    mov     di, ax
    mov     al, [di + CUR_BUF]       ; al = current full state
    shr     al, 1                    ; al = old_alive bit (0 or 1)

    or      al, al
    jz      .dead_cell

    ; Old alive: survives iff count==2 or count==3.
    cmp     bl, 2
    je      .stays_alive
    cmp     bl, 3
    je      .stays_alive
    ; Dies.
    mov     byte [di + NEXT_BUF], 1  ; just-died
    jmp     .advance
.stays_alive:
    mov     byte [di + NEXT_BUF], 2  ; long-alive
    jmp     .advance

.dead_cell:
    ; Old dead: born iff count==3.
    cmp     bl, 3
    je      .born
    mov     byte [di + NEXT_BUF], 0  ; long-dead
    jmp     .advance
.born:
    mov     byte [di + NEXT_BUF], 3  ; newborn

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
;
; Per-cell encoding (output bytes):
;   state 0 long-dead  : ' '                          (1 byte)
;   state 1 just-died  : ESC '[' '9' '0' 'm' '.'      (6 bytes, dim gray, faint dot)
;   state 2 long-alive : ESC '[' Nh Nl 'm' '#'        (6 bytes, gen color, full density)
;   state 3 newborn    : ESC '[' '9' '7' 'm' '#'      (6 bytes, bright white, ignition)
render_grid:
    mov     word [row_var], 0
.row:
    push    cs
    pop     ds
    mov     di, row_buf
    mov     word [col_var], 0

    mov     ax, [row_var]
    mov     cl, WIDTH
    mul     cl
    mov     [row_base_var], ax

.cell:
    mov     bx, [row_base_var]
    add     bx, [col_var]
    mov     al, [bx + CUR_BUF]

    ; Switch on al = 0,1,2,3
    or      al, al
    jz      .dead0
    cmp     al, 1
    je      .died
    cmp     al, 2
    je      .alive_long
    ; al == 3 (newborn)
    mov     byte [di], 0x1B
    mov     byte [di + 1], '['
    mov     byte [di + 2], '9'
    mov     byte [di + 3], '7'
    mov     byte [di + 4], 'm'
    mov     byte [di + 5], '#'
    add     di, 6
    jmp     .cell_done

.alive_long:
    mov     byte [di], 0x1B
    mov     byte [di + 1], '['
    mov     ax, [color_var]
    mov     [di + 2], ax
    mov     byte [di + 4], 'm'
    mov     byte [di + 5], '#'
    add     di, 6
    jmp     .cell_done

.died:
    mov     byte [di], 0x1B
    mov     byte [di + 1], '['
    mov     byte [di + 2], '9'
    mov     byte [di + 3], '0'
    mov     byte [di + 4], 'm'
    mov     byte [di + 5], '.'
    add     di, 6
    jmp     .cell_done

.dead0:
    mov     byte [di], ' '
    inc     di

.cell_done:
    inc     word [col_var]
    mov     ax, [col_var]
    cmp     ax, WIDTH
    jl      .cell

    ; Row terminator: CRLF + '$'.
    mov     byte [di], 0x0D
    mov     byte [di + 1], 0x0A
    mov     byte [di + 2], '$'

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
esc_home:    db 0x1B,'[','H','$'
esc_reset:   db 0x1B,'[','0','m',0x0D,0x0A,'$'

; "Long-alive" cell color cycles through 8 ANSI codes. Two-byte ASCII
; pair stored low byte first so `mov word [...]` pulls it into AX
; with AL=Nh, AH=Nl, then `mov [di+2], ax` writes them in order.
color_table:
    db '3','4'                       ; blue
    db '3','6'                       ; cyan
    db '3','2'                       ; green
    db '3','3'                       ; yellow
    db '3','1'                       ; red
    db '3','5'                       ; magenta
    db '9','6'                       ; bright cyan
    db '9','3'                       ; bright yellow

neighbor_offsets:
    db -1, -1
    db -1,  0
    db -1,  1
    db  0, -1
    db  0,  1
    db  1, -1
    db  1,  0
    db  1,  1

; Per-frame state.
gen_var:        dw 0
row_var:        dw 0
col_var:        dw 0
row_base_var:   dw 0
color_var:      dw 0
rng_seed_var:   dw 0

; Row output buffer: 78 cells x max 6 bytes + row terminator (3) = 471 bytes.
row_buf:    times WIDTH*6 + 3 db 0
