; mandel.asm — Q4.12 fixed-point ASCII Mandelbrot for espDos.
;
; Loaded as MANDEL.COM by asm/loader.asm. Renders an ASCII Mandelbrot
; to the 86-DOS console via INT 21h AH=02h (CONOUT) and terminates
; via INT 20h. Origin 100h (.COM convention).
;
; Fixed-point: Q4.12 (signed 16-bit, 4 integer + 12 fractional bits,
; range approx +-8.0). Multiplication: 16x16 IMUL gives a 32-bit
; signed product (Q8.24 in DX:AX); we right-shift by 12 to renormalize
; to Q4.12 by combining the low 12 bits of DX with the high 4 bits of
; AX:
;
;   AX = (DX << 4) | (AX >> 12)
;
; The escape test is |z|^2 > 4. With Q4.12, 4.0 = 0x4000.
;
; Register hygiene: 86-DOS 1.0's INT 21h handlers do NOT preserve
; registers across the call. (Empirically verified: an early version
; of this file kept the column counter in DI and saw it clobbered to
; >=WIDTH after the first character of each row, producing one '@' per
; row and a 24-line tall x 1-wide output instead of 78x24.) So all
; loop state lives in memory variables, reloaded between INT 21h calls.
; The inner zr*zr / zi*zi / zr*zi work happens between consecutive
; INT 21h's so it can use registers freely.
;
; Grid: WIDTH x HEIGHT chars + CRLF per row. Character ramp by escape
; iteration: ' .:-=+*#%@' (10 chars). In-set (didn't escape) -> '@'.

bits 16
cpu 8086
org 0x100

WIDTH      equ 78
HEIGHT     equ 24
MAX_ITER   equ 24

; Q4.12 constants
ONE        equ 0x1000     ;  1.0
TWO        equ 0x2000     ;  2.0
FOUR       equ 0x4000     ;  4.0
NEG_TWO    equ 0xE000     ; -2.0  (signed)
NEG_ONE    equ 0xF000     ; -1.0  (signed)

; cx range [-2.0, +0.5], step = 2.5*4096/78 = 131.28..
CX_STEP    equ 131
; cy range [-1.0, +1.0], step = 2.0*4096/24 = 341.33..
CY_STEP    equ 341

; ---------- entry ----------
start:
    ; .COM convention: DS=ES=CS=PSP segment on entry. The kernel's
    ; INT 21h handler may change DS/ES while servicing, so we reload
    ; from CS at the top of each iteration that follows an INT 21h.
    push    cs
    pop     ds
    mov     word [cy_var], NEG_ONE
    mov     word [row_var], 0

row_loop:
    mov     word [cx_var], NEG_TWO
    mov     word [col_var], 0

col_loop:
    ; --- inner Mandelbrot iteration for one pixel ---
    xor     ax, ax
    mov     [zr_var], ax         ; zr = 0
    mov     [zi_var], ax         ; zi = 0
    mov     [iter_var], ax       ; iter = 0

    ; --- cardioid + period-2 disk early-reject ---
    ; Both regions are mathematically inside the Mandelbrot set, so any
    ; point in either can skip the iteration loop and be marked in-set.
    ; Saves up to MAX_ITER * 3 IMULs for ~30% of pixels.
    ;
    ; No INT 21h on this path, so registers are safe across the test.
    ; q12_norm preserves CX (push/pop), so we can park cy^2 there.

    ; Period-2 disk: (cx + 1.0)^2 + cy^2 < 1/16   (Q4.12: < 0x100)
    mov     ax, [cy_var]
    mov     bx, ax
    imul    bx                   ; DX:AX = cy*cy
    call    q12_norm             ; AX = cy^2 (Q4.12)
    mov     cx, ax               ; CX = cy^2  (cached for cardioid)

    mov     ax, [cx_var]
    add     ax, ONE              ; cx + 1.0
    mov     bx, ax
    imul    bx                   ; (cx+1)^2
    call    q12_norm
    add     ax, cx               ; (cx+1)^2 + cy^2
    cmp     ax, 0x100            ; 1/16 in Q4.12
    jl      inside_set           ; signed: result is small + non-negative

    ; Cardioid: q = dx^2 + cy^2 where dx = cx - 0.25
    ;          point is inside iff  q*(q + dx) < cy^2 / 4
    ; Gate the multiplication on q < 1.0 (Q4.12: 0x1000) to avoid
    ; Q4.12 overflow in q*(q+dx) when q is large (definitely outside).
    mov     ax, [cx_var]
    sub     ax, 0x400            ; dx = cx - 0.25
    mov     [dx_var], ax
    mov     bx, ax
    imul    bx                   ; dx*dx
    call    q12_norm             ; AX = dx^2
    add     ax, cx               ; q = dx^2 + cy^2  (CX still holds cy^2)
    cmp     ax, ONE              ; gate: only test if q < 1.0
    jge     cardioid_skip
    mov     [q_var], ax          ; save q
    add     ax, [dx_var]         ; q + dx
    mov     bx, ax
    mov     ax, [q_var]
    imul    bx                   ; q * (q + dx)
    call    q12_norm             ; AX = q*(q+dx)
    ; compare to cy^2 / 4
    mov     bx, cx               ; cy^2
    sar     bx, 1
    sar     bx, 1                ; cy^2 >> 2 = cy^2 * 0.25
    cmp     ax, bx
    jl      inside_set
cardioid_skip:
    jmp     iter_loop

inside_set:
    mov     word [iter_var], MAX_ITER
    jmp     escaped

iter_loop:
    ; zr2 = zr * zr  (Q4.12)
    mov     ax, [zr_var]
    mov     bx, ax
    imul    bx                   ; DX:AX = zr*zr
    call    q12_norm             ; AX = Q4.12 result
    mov     [zr2_var], ax

    ; zi2 = zi * zi
    mov     ax, [zi_var]
    mov     bx, ax
    imul    bx
    call    q12_norm
    mov     [zi2_var], ax

    ; if zr2 + zi2 > 4.0 (signed) -> escape
    mov     ax, [zr2_var]
    add     ax, [zi2_var]
    cmp     ax, FOUR
    jg      escaped

    ; zrzi = zr * zi
    mov     ax, [zr_var]
    mov     bx, [zi_var]
    imul    bx
    call    q12_norm             ; AX = zr*zi
    shl     ax, 1                ; *2
    add     ax, [cy_var]         ; new_zi = 2*zr*zi + cy
    mov     [new_zi_var], ax

    ; new_zr = zr2 - zi2 + cx
    mov     ax, [zr2_var]
    sub     ax, [zi2_var]
    add     ax, [cx_var]
    mov     [zr_var], ax

    mov     ax, [new_zi_var]
    mov     [zi_var], ax

    inc     word [iter_var]
    mov     ax, [iter_var]
    cmp     ax, MAX_ITER
    jl      iter_loop

    ; reached MAX_ITER without escape -> in-set, use last char
    mov     ax, MAX_ITER
    mov     [iter_var], ax

escaped:
    ; ramp index = (iter * 10) / MAX_ITER, clamped to 0..9
    mov     ax, [iter_var]
    mov     bx, 10
    mul     bx                   ; DX:AX = iter*10 (unsigned, fits 16-bit)
    mov     bx, MAX_ITER
    xor     dx, dx
    div     bx                   ; AX = iter*10/MAX_ITER
    cmp     ax, 9
    jbe     ramp_ok
    mov     ax, 9
ramp_ok:
    ; AX holds ramp index 0..9; look up the char and store it in the
    ; row buffer at offset col_var. No INT 21h yet — we batch the
    ; whole row into one AH=09h PRTBUF call below. See
    ; docs/mandelbrot-performance.md for the cost analysis: per-char
    ; AH=02h CONOUT was ~209 instructions of dispatch overhead;
    ; per-row AH=09h amortizes most of that across 78 chars.
    mov     bx, ax
    mov     al, [ramp + bx]
    mov     bx, [col_var]
    mov     [row_buf + bx], al

    ; advance cx (no INT 21h here, so no DS reload)
    mov     ax, [cx_var]
    add     ax, CX_STEP
    mov     [cx_var], ax

    inc     word [col_var]
    mov     ax, [col_var]
    cmp     ax, WIDTH
    jl      col_loop

    ; row complete — emit row_buf (WIDTH chars + CR + LF) via one
    ; AH=09h PRTBUF call. The buffer is `$`-terminated in its data
    ; declaration below.
    mov     ah, 0x09
    mov     dx, row_buf
    int     0x21
    ; reload DS — INT 21h handler clobbers it
    push    cs
    pop     ds

    ; advance cy
    mov     ax, [cy_var]
    add     ax, CY_STEP
    mov     [cy_var], ax

    inc     word [row_var]
    mov     ax, [row_var]
    cmp     ax, HEIGHT
    jl      row_loop

    int     0x20

; ---------- helpers ----------
; q12_norm: turn DX:AX (signed Q8.24 product) into AX = Q4.12.
;   AX = (DX << 4) | (AX >> 12)
; Clobbers CX (saved/restored).
q12_norm:
    push    cx
    mov     cl, 4
    shl     dx, cl               ; DX <<= 4 (high nibble drops)
    mov     cl, 12
    shr     ax, cl               ; AX >>= 12 (zero-fill; high 12 bits cleared)
    or      ax, dx
    pop     cx
    ret

; ---------- data ----------
ramp:        db ' .:-=+*#%@'
zr_var:      dw 0
zi_var:      dw 0
zr2_var:     dw 0
zi2_var:     dw 0
new_zi_var:  dw 0
cx_var:      dw 0
cy_var:      dw 0
col_var:     dw 0
row_var:     dw 0
iter_var:    dw 0
dx_var:      dw 0          ; cardioid: cx - 0.25
q_var:       dw 0          ; cardioid: q = dx^2 + cy^2

; row_buf holds one row of pixels followed by CR LF and the AH=09h
; PRTBUF terminator '$'. The first WIDTH bytes get overwritten each
; row in the inner loop; the trailing CR/LF/'$' stays put.
row_buf:     times WIDTH db ' '
             db 0x0D, 0x0A, '$'
