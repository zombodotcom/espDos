; shell.asm — SHELL.COM transient for espDos.
;
; First interactive program *and* re-entrant: launches a child
; transient, regains control after the child's INT 20h, loops back
; to the menu. Implements the "command processor" role 86-DOS 1.00
; never had built into its kernel.
;
; Re-entry mechanism:
;   1. On startup, save the loader-installed IVT[20h] (= bootstub
;      halt loop in BOOT_SEG). 'q' restores this and INT 20h's, so
;      the system halts cleanly when the user is done.
;   2. Before each child launch, point IVT[20h] at on_child_exit
;      (USER_SEG = our own CS). Child's INT 20h then re-enters us.
;   3. on_child_exit immediately switches SS:SP back to our saved
;      pre-launch stack (the INT pushed flags+CS+IP onto the child's
;      stack, which we abandon — those bytes live in CHILD_SEG and
;      don't matter once we're back). DS:ES restored to USER_SEG.
;
; Why AH=07 for input: AH=01 (CONIN) routes through INCHK, which the
; kernel also invokes during every CONOUT as a Ctrl-C/S/P/N "input
; snoop." Combined with our auto-feed BIOSIN, that snoop ate the
; AUTOPICK digit during menu print, leaving SHELL's later AH=01 to
; block forever. AH=07 (RAWINP) is a direct CALL BIOSSEG:BIOSIN with
; no snoop layer.

bits 16
org 0x100

CHILD_SEG       equ 0x3000
HELLO_SECTOR    equ 11
MANDEL_SECTOR   equ 12
COUNT_SECTOR    equ 13
JULIA_SECTOR    equ 15           ; cluster 6, spans 3 sectors

INT20_VEC       equ 0x20*4

start:
    ; DS=ES=SS=USER_SEG and SP=0xFFFE on entry from loader.asm.

    ; Save the loader's IVT[20h] so we can restore it on quit.
    cli
    xor  ax, ax
    mov  es, ax
    mov  ax, [es:INT20_VEC]
    mov  [prev_int20_off], ax
    mov  ax, [es:INT20_VEC + 2]
    mov  [prev_int20_seg], ax
    sti

shell_loop:
    ; Print menu.
    push cs
    pop  ds
    push cs
    pop  es
    mov  ah, 0x09
    mov  dx, menu
    int  0x21

prompt:
    ; AH=07 = direct BIOSIN, bypasses INCHK input-snoop.
    mov  ah, 0x07
    int  0x21
    ; Echo the chosen char + CRLF.
    mov  dl, al
    push ax
    mov  ah, 0x02
    int  0x21
    mov  dl, 0x0D
    mov  ah, 0x02
    int  0x21
    mov  dl, 0x0A
    mov  ah, 0x02
    int  0x21
    pop  ax

    cmp  al, '1'
    je   pick_hello
    cmp  al, '2'
    je   pick_count
    cmp  al, '3'
    je   pick_mandel
    cmp  al, '4'
    je   pick_julia
    jmp  quit

pick_hello:
    mov  dx, HELLO_SECTOR
    mov  cx, 1
    jmp  load_and_run
pick_count:
    mov  dx, COUNT_SECTOR
    mov  cx, 1
    jmp  load_and_run
pick_mandel:
    mov  dx, MANDEL_SECTOR
    mov  cx, 1
    jmp  load_and_run
pick_julia:
    mov  dx, JULIA_SECTOR
    mov  cx, 3                   ; julia.bin = 1082 bytes -> 3 sectors

load_and_run:
    ; Save SP so on_child_exit can switch back to our stack.
    mov  [shell_sp_save], sp
    ; Save sector count too — DS clobber below would otherwise lose it.
    mov  [shell_count_save], cx

    ; Install IVT[20h] = (on_child_exit, our CS = USER_SEG).
    cli
    xor  ax, ax
    mov  es, ax
    mov  word [es:INT20_VEC], on_child_exit
    mov  ax, cs
    mov  word [es:INT20_VEC + 2], ax
    sti

    ; BIOSREAD CX sectors into CHILD_SEG:0x100.
    ;   AL=drive, BX=offset, CX=count, DX=sector, DS=buffer seg.
    mov  ax, CHILD_SEG
    mov  ds, ax
    mov  al, 0
    mov  bx, 0x100
    mov  cx, [cs:shell_count_save]
    call 0x0040:0x0015           ; BIOSSEG:BIOSREAD
    jc   load_failed

    ; Stand up the child like a fresh transient.
    mov  ax, CHILD_SEG
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0xFFFE
    jmp  word CHILD_SEG:0x100

on_child_exit:
    ; Re-entry from the child's INT 20h. CPU pushed flags+CS+IP onto
    ; the *child's* stack (SS=CHILD_SEG). Switch our stack back first.
    cli
    mov  ax, cs
    mov  ss, ax
    mov  sp, [shell_sp_save]
    mov  ds, ax
    mov  es, ax
    sti

    ; Print a CRLF before the next menu so it doesn't butt up against
    ; the child's last line of output.
    mov  ah, 0x09
    mov  dx, between
    int  0x21
    jmp  shell_loop

quit:
    ; Restore loader's IVT[20h] (= bootstub halt) and exit.
    cli
    xor  ax, ax
    mov  es, ax
    mov  ax, [prev_int20_off]
    mov  [es:INT20_VEC], ax
    mov  ax, [prev_int20_seg]
    mov  [es:INT20_VEC + 2], ax
    sti
    int  0x20

load_failed:
    push cs
    pop  ds
    mov  ah, 0x09
    mov  dx, fail_msg
    int  0x21
    jmp  quit

menu:
    db 0x0D, 0x0A
    db 'espDos shell', 0x0D, 0x0A
    db '  1) HELLO   - banner', 0x0D, 0x0A
    db '  2) COUNT   - 1..50', 0x0D, 0x0A
    db '  3) MANDEL  - ASCII fractal', 0x0D, 0x0A
    db '  4) JULIA   - color animated set', 0x0D, 0x0A
    db '  q) quit', 0x0D, 0x0A
    db '> $'

between:
    db 0x0D, 0x0A, '$'

fail_msg:
    db 0x0D, 0x0A, 'load failed', 0x0D, 0x0A, '$'

prev_int20_off:    dw 0
prev_int20_seg:    dw 0
shell_sp_save:     dw 0
shell_count_save:  dw 0
