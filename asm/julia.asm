; julia.asm — animated 16-color Julia set for espDos.
;
; Loaded as JULIA.COM. Renders a sequence of Julia-set frames to the
; 86-DOS console using INT 21h AH=09h PRTBUF (one row per call). The
; parameter c walks a circle of radius 0.7885 around the origin in
; the c-plane (a curve that crosses many of the most visually
; interesting Julia-set "atlases" — disconnected dust, dendrites,
; spirals, seahorses). Between frames we emit ESC[H to home the
; cursor so the next frame overwrites the previous in place.
;
; Math: same Q4.12 IMUL kernel as asm/mandel.asm. The only difference
; is the per-pixel setup: Mandelbrot is z0=0, c=pixel-coords; Julia
; is z0=pixel-coords, c=animated-constant. We drop Mandelbrot's
; cardioid + period-2 disk early-reject — those are Mandelbrot-only
; shortcuts that don't apply to Julia. Plain iteration suffices.
;
; Color: 16-color basic ANSI (foreground codes 30..37 + bright 90..97)
; mapped onto MAX_ITER iteration counts. Each pixel emits a 6-byte
; sequence "ESC [ N N m C" where NN is the 2-digit color code and C
; is a density char from a hand-picked ramp. Row terminator
; ESC[0m\r\n$ resets color and ends the AH=09 string.
;
; Origin 100h (.COM). Same data-in-memory loop discipline as mandel:
; INT 21h handlers don't preserve registers across calls, so all
; loop state lives in `dw` variables and is reloaded between calls.

bits 16
cpu 8086
org 0x100

WIDTH      equ 78
HEIGHT     equ 24
MAX_ITER   equ 24

; Q4.12 constants
FOUR       equ 0x4000

; Pixel coordinate range for z0:
;   zx in [-1.6, +1.6], step = 2*1.6*4096/78 = 168.10
;   zy in [-1.0, +1.0], step = 2*1.0*4096/24 = 341.33
ZX_START   equ 0xE666     ; -1.6 in Q4.12
ZX_STEP    equ 168
ZY_START   equ 0xF000     ; -1.0
ZY_STEP    equ 341

; ---------- entry ----------
start:
    push    cs
    pop     ds

    mov     word [frame_var], 0

frame_loop:
    ; Load c from c_table[frame]. Each entry is 4 bytes (cx_word, cy_word).
    mov     bx, [frame_var]
    shl     bx, 1
    shl     bx, 1                   ; bx = frame * 4
    mov     ax, [c_table + bx]
    mov     [c_real_var], ax
    mov     ax, [c_table + bx + 2]
    mov     [c_imag_var], ax

    ; Cursor home before each frame. Don't full-clear — that flashes
    ; the scrollback. Just home and overwrite.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, esc_home
    int     0x21
    push    cs
    pop     ds

    mov     word [zy_var], ZY_START
    mov     word [row_var], 0

row_loop:
    mov     word [zx_var], ZX_START
    mov     word [col_var], 0
    mov     word [bufptr_var], row_buf

col_loop:
    ; Initialize z0 = (zx, zy), iter = 0.
    mov     ax, [zx_var]
    mov     [zr_var], ax
    mov     ax, [zy_var]
    mov     [zi_var], ax
    mov     word [iter_var], 0

iter_loop:
    ; zr2 = zr*zr  (Q4.12 * Q4.12 -> Q8.24, normalize back to Q4.12)
    mov     ax, [zr_var]
    imul    word [zr_var]
    call    q12_norm
    mov     [zr2_var], ax

    ; zi2 = zi*zi
    mov     ax, [zi_var]
    imul    word [zi_var]
    call    q12_norm
    mov     [zi2_var], ax

    ; Escape: zr2 + zi2 > 4 ?
    add     ax, [zr2_var]
    cmp     ax, FOUR
    jg      escaped

    ; new_zi = 2 * zr * zi + c_imag
    mov     ax, [zr_var]
    imul    word [zi_var]
    call    q12_norm
    shl     ax, 1
    add     ax, [c_imag_var]
    mov     [new_zi_var], ax

    ; zr = zr2 - zi2 + c_real
    mov     ax, [zr2_var]
    sub     ax, [zi2_var]
    add     ax, [c_real_var]
    mov     [zr_var], ax

    ; zi = new_zi
    mov     ax, [new_zi_var]
    mov     [zi_var], ax

    inc     word [iter_var]
    mov     ax, [iter_var]
    cmp     ax, MAX_ITER
    jl      iter_loop

    ; Reached MAX_ITER without escape -> in-set
    mov     ax, MAX_ITER
    mov     [iter_var], ax

escaped:
    ; pal_idx = iter * 16 / MAX_ITER (clamped 0..15).
    ; With MAX_ITER=24 this maps 0->0, 12->8, 23->15, 24->16 (clamped).
    mov     ax, [iter_var]
    mov     bx, 16
    mul     bx
    mov     bx, MAX_ITER
    xor     dx, dx
    div     bx
    cmp     ax, 15
    jbe     pal_ok
    mov     ax, 15
pal_ok:
    ; ax = palette index 0..15. Each palette entry is 6 bytes:
    ;   ESC '[' Nh Nl 'm' C   (Nh,Nl ASCII digits, C density char)
    ; Copy 6 bytes from palette[ax*6] into row_buf at bufptr_var.
    mov     bx, ax
    shl     bx, 1
    shl     bx, 1
    add     bx, ax
    add     bx, ax                  ; bx = ax * 6
    add     bx, palette
    mov     si, bx
    mov     di, [bufptr_var]
    mov     cx, 6
    cld
    rep     movsb
    mov     [bufptr_var], di

    ; advance zx
    mov     ax, [zx_var]
    add     ax, ZX_STEP
    mov     [zx_var], ax

    inc     word [col_var]
    mov     ax, [col_var]
    cmp     ax, WIDTH
    jl      col_loop

    ; Row complete. Append the row terminator (ESC[0m\r\n$) at bufptr.
    mov     di, [bufptr_var]
    mov     si, row_term
    mov     cx, row_term_len
    cld
    rep     movsb

    ; Emit the row.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, row_buf
    int     0x21
    push    cs
    pop     ds

    ; advance zy
    mov     ax, [zy_var]
    add     ax, ZY_STEP
    mov     [zy_var], ax

    inc     word [row_var]
    mov     ax, [row_var]
    cmp     ax, HEIGHT
    jl      row_loop

    ; Frame done.
    inc     word [frame_var]
    mov     ax, [frame_var]
    cmp     ax, NUM_FRAMES
    jl      frame_loop

    ; Reset color, then exit so the kernel halt loop doesn't pick up
    ; coloring from a half-printed escape.
    push    cs
    pop     ds
    mov     ah, 0x09
    mov     dx, esc_reset
    int     0x21
    int     0x20

; ---------- helpers ----------
; q12_norm: turn DX:AX (signed Q8.24) into AX = Q4.12.
;   AX = (DX << 4) | (AX >> 12)
; Clobbers CX (saved/restored).
q12_norm:
    push    cx
    mov     cl, 4
    shl     dx, cl
    mov     cl, 12
    shr     ax, cl
    or      ax, dx
    pop     cx
    ret

; ---------- data ----------
; ANSI palette: 16 entries x 6 bytes each. Color codes ramp blue ->
; cyan -> green -> yellow -> magenta -> red -> white. The fill
; character ramps from ' ' to '@' alongside the color, so a terminal
; that drops ANSI still shows the iteration-count shape.
palette:
    db 0x1B,'[','3','4','m',' '   ;  0  blue        space
    db 0x1B,'[','9','4','m','.'   ;  1  bright blue '.'
    db 0x1B,'[','3','6','m',':'   ;  2  cyan        ':'
    db 0x1B,'[','9','6','m','-'   ;  3  bright cyan '-'
    db 0x1B,'[','3','2','m','='   ;  4  green       '='
    db 0x1B,'[','9','2','m','+'   ;  5  bright green '+'
    db 0x1B,'[','3','3','m','*'   ;  6  yellow      '*'
    db 0x1B,'[','9','3','m','#'   ;  7  bright yellow '#'
    db 0x1B,'[','3','5','m','%'   ;  8  magenta     '%'
    db 0x1B,'[','9','5','m','@'   ;  9  bright magenta '@'
    db 0x1B,'[','3','1','m','&'   ; 10  red         '&'
    db 0x1B,'[','9','1','m','X'   ; 11  bright red  'X'
    db 0x1B,'[','3','7','m','W'   ; 12  white       'W'
    db 0x1B,'[','9','7','m','M'   ; 13  bright white 'M'
    db 0x1B,'[','3','0','m','@'   ; 14  black       '@'  (fades into bg)
    db 0x1B,'[','3','0','m',' '   ; 15  black       ' '  (in-set, invisible)

; Row terminator: reset color + CRLF + AH=09 sentinel.
row_term:    db 0x1B,'[','0','m',0x0D,0x0A,'$'
row_term_len equ $ - row_term

; Frame separator strings.
esc_home:    db 0x1B,'[','H','$'
esc_reset:   db 0x1B,'[','0','m',0x0D,0x0A,'$'

; c-orbit table: 30 (c_real, c_imag) pairs in Q4.12, generated at
; build time by python (see asm/build_kernel.sh). Walks a circle of
; radius 0.7885 around the origin in 12-degree steps.
NUM_FRAMES equ 30
c_table:
    dw 0x0C9E, 0x0000   ; theta=  0 deg
    dw 0x0C57, 0x029F   ; theta= 12 deg
    dw 0x0B86, 0x0522   ; theta= 24 deg
    dw 0x0A35, 0x076A   ; theta= 36 deg
    dw 0x0871, 0x0960   ; theta= 48 deg
    dw 0x064F, 0x0AED   ; theta= 60 deg
    dw 0x03E6, 0x0C00   ; theta= 72 deg
    dw 0x0152, 0x0C8C   ; theta= 84 deg
    dw 0xFEAE, 0x0C8C   ; theta= 96 deg
    dw 0xFC1A, 0x0C00   ; theta=108 deg
    dw 0xF9B1, 0x0AED   ; theta=120 deg
    dw 0xF78F, 0x0960   ; theta=132 deg
    dw 0xF5CB, 0x076A   ; theta=144 deg
    dw 0xF47A, 0x0522   ; theta=156 deg
    dw 0xF3A9, 0x029F   ; theta=168 deg
    dw 0xF362, 0x0000   ; theta=180 deg
    dw 0xF3A9, 0xFD61   ; theta=192 deg
    dw 0xF47A, 0xFADE   ; theta=204 deg
    dw 0xF5CB, 0xF896   ; theta=216 deg
    dw 0xF78F, 0xF6A0   ; theta=228 deg
    dw 0xF9B1, 0xF513   ; theta=240 deg
    dw 0xFC1A, 0xF400   ; theta=252 deg
    dw 0xFEAE, 0xF374   ; theta=264 deg
    dw 0x0152, 0xF374   ; theta=276 deg
    dw 0x03E6, 0xF400   ; theta=288 deg
    dw 0x064F, 0xF513   ; theta=300 deg
    dw 0x0871, 0xF6A0   ; theta=312 deg
    dw 0x0A35, 0xF896   ; theta=324 deg
    dw 0x0B86, 0xFADE   ; theta=336 deg
    dw 0x0C57, 0xFD61   ; theta=348 deg

; Per-frame state.
zr_var:        dw 0
zi_var:        dw 0
zr2_var:       dw 0
zi2_var:       dw 0
new_zi_var:    dw 0
zx_var:        dw 0
zy_var:        dw 0
c_real_var:    dw 0
c_imag_var:    dw 0
col_var:       dw 0
row_var:       dw 0
iter_var:      dw 0
frame_var:     dw 0
bufptr_var:    dw 0

; Row buffer: 78 pixels x 6 bytes + row terminator (7 bytes) = 475 bytes.
; Reserved as raw bss; we fill it in-place each row.
row_buf:    times WIDTH*6 + row_term_len db 0
