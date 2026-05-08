; primes.asm — PRIMES.COM. Sieve of Eratosthenes through N=200.
;
; Prints each prime as a 3-char space-padded decimal, separated by a
; trailing space, 10 primes per line. INT 21h call 02h only — no
; input, no FCB, no DMA. Exits via INT 20h back to SHELL.
;
; Memory layout: code + data live below offset ~0x300 in CHILD_SEG;
; the 201-byte sieve array piggybacks on the same .bin (zeroed at
; load by the file image, then again at runtime for safety).

bits 16
org 0x100

N           equ 200             ; upper bound (inclusive) for primes
SQRT_N      equ 15              ; ceil(sqrt(200)) — outer loop bound

start:
    ; Zero the sieve. mark[i]=0 means "prime so far"; we set 1 when
    ; we hit a composite. Doing this at runtime means PRIMES is robust
    ; to a re-run inside SHELL (CHILD_SEG memory carries over).
    push cs
    pop  ds
    push cs
    pop  es
    mov  di, sieve
    mov  cx, N + 1
    xor  al, al
    rep  stosb

    ; Sieve the composites. Outer loop walks i = 2..SQRT_N; for each
    ; unmarked i, mark every 2i, 3i, 4i, ... up to N.
    mov  cx, 2
.s_outer:
    cmp  cx, SQRT_N
    ja   .print_all
    mov  bx, sieve
    add  bx, cx
    cmp  byte [bx], 0
    jne  .s_next                 ; already marked composite, skip
    mov  ax, cx
    add  ax, ax                  ; first composite of cx is 2*cx
.s_inner:
    cmp  ax, N
    ja   .s_next
    mov  bx, sieve
    add  bx, ax
    mov  byte [bx], 1
    add  ax, cx
    jmp  .s_inner
.s_next:
    inc  cx
    jmp  .s_outer

.print_all:
    ; Walk 2..N; print every mark[i]==0 entry.
    mov  cx, 2
    xor  bx, bx                  ; bx = primes-on-current-line counter
.p_outer:
    cmp  cx, N
    ja   .done
    mov  si, sieve
    add  si, cx
    cmp  byte [si], 0
    jne  .p_next
    mov  ax, cx
    call print_3digit
    mov  ah, 0x02
    mov  dl, ' '
    int  0x21
    inc  bx
    cmp  bx, 10
    jb   .p_next
    xor  bx, bx
    mov  ah, 0x02
    mov  dl, 0x0D
    int  0x21
    mov  dl, 0x0A
    int  0x21
.p_next:
    inc  cx
    jmp  .p_outer

.done:
    ; Final CRLF before returning.
    mov  ah, 0x02
    mov  dl, 0x0D
    int  0x21
    mov  dl, 0x0A
    int  0x21
    int  0x20

; print_3digit — print AX (range 0..999) as 3 ASCII chars, leading
; zeros replaced by spaces.
print_3digit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov  bx, 100
    xor  dx, dx
    div  bx                      ; AX = hundreds, DX = AX % 100
    add  al, '0'
    mov  [num_buf + 0], al
    mov  ax, dx
    mov  bx, 10
    xor  dx, dx
    div  bx                      ; AX = tens, DX = ones
    add  al, '0'
    add  dl, '0'
    mov  [num_buf + 1], al
    mov  [num_buf + 2], dl
    cmp  byte [num_buf + 0], '0'
    jne  .out
    mov  byte [num_buf + 0], ' '
    cmp  byte [num_buf + 1], '0'
    jne  .out
    mov  byte [num_buf + 1], ' '
.out:
    mov  cx, 3
    mov  si, num_buf
.l:
    mov  dl, [si]
    mov  ah, 0x02
    int  0x21
    inc  si
    loop .l
    pop  si
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

num_buf:    times 4   db 0
sieve:      times N+1 db 0
