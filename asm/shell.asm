; shell.asm — SHELL.COM, the espDos command processor.
;
; Implements the COMMAND.COM role 86-DOS 1.0 never built into the
; kernel: prints "A>", reads a typed line, parses it into a command
; + tail, dispatches against a built-in table (EXIT, ...), and on
; miss falls through to external_load which OPENs $CMD.COM via FCB,
; SEQRDs it into CHILD_SEG:0x100, and JMPs to it.
;
; Re-entry mechanism (lets a child program return to the prompt):
;   1. On startup, save the loader-installed IVT[20h] (= bootstub
;      halt loop). EXIT restores this and INT 20h's so the system
;      halts cleanly when the user is done.
;   2. Before each child launch, install IVT[20h] = on_child_exit
;      (in our own CS). Child's INT 20h then re-enters here.
;   3. on_child_exit switches SS:SP back to our saved pre-launch
;      stack (the INT pushed flags+CS+IP onto the child's stack,
;      which we abandon — those bytes live in CHILD_SEG and don't
;      matter once we're back). DS:ES are restored to USER_SEG.
;
; Why AH=07h for input + AH=06h DL=char for echo: the kernel's BUFIN
; (AH=0Ah) and CONIN (AH=01h) both route through INCHK, the
; Ctrl-C/S/P/N snoop. CONOUT (AH=02h) routes through STATCHK which
; also calls INCHK. RAWINP (AH=07h) and RAWIO (AH=06h) bypass both
; layers, so AUTOPICK auto-feed bytes are never silently eaten.

bits 16
org 0x100

CHILD_SEG       equ 0x3000

INT20_VEC       equ 0x20*4
INPUT_BUF_LEN   equ 80           ; DOS-classic line length
CMD_BUF_LEN     equ 16           ; longest built-in/external name

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

    ; One-time banner. Then drop into the prompt loop.
    push cs
    pop  ds
    push cs
    pop  es
    mov  ah, 0x09
    mov  dx, banner_str
    int  0x21

shell_loop:
    push cs
    pop  ds
    push cs
    pop  es
    mov  ah, 0x09
    mov  dx, prompt_str
    int  0x21

prompt:
    ; Read a typed line. read_line uses AH=07h (RAWINP, no snoop) for
    ; input and AH=06h DL=char (RAWIO, no snoop) for echo, so neither
    ; the AUTOPICK auto-feed nor a real keystroke gets eaten by the
    ; kernel's INCHK/STATCHK Ctrl-C/S/P/N snoop layer. AL = first char
    ; of the buffer (0 if line was empty); CX = length; ZF=1 if empty.
    call read_line
    test cx, cx
    jz   shell_loop              ; empty line: re-prompt

    ; Built-in dispatch first. CF=1 means a handler ran (its jmp/ret
    ; returned us here); fall through only on CF=0 (no match).
    call parse_command
    cmp  word [cmd_len], 0
    je   shell_loop              ; whitespace-only line: re-prompt
    call dispatch
    jc   shell_loop

    ; No built-in match. Try loading a .COM file from disk.
    mov  si, cmd_buf
    call external_load
    jc   .not_found              ; OPEN failed or invalid name
    ; external_load on success runs the child and never returns here.
    ; (on_child_exit re-enters shell_loop.)

.not_found:
    push cs
    pop  ds
    mov  ah, 0x09
    mov  dx, bad_cmd_msg
    int  0x21
    jmp  shell_loop

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

; -----------------------------------------------------------------
; read_line — typed-line input with backspace + Ctrl-C support.
;
; AH=07h (RAWINP) for input bypasses INCHK; AH=06h DL=char (RAWIO)
; for echo bypasses STATCHK. Neither path snoops, so AUTOPICK bytes
; are consumed cleanly and real keystrokes echo correctly.
;
; Caller must have DS = ES = CS (USER_SEG). Not re-entrant — uses
; the static input_buf below.
;
; Exit: input_buf zero-terminated; CX = length; AL = input_buf[0]
;       (0 if empty); ZF=1 if empty.
read_line:
    push di
    mov  di, input_buf
    xor  cx, cx
.loop:
    mov  ah, 0x07
    int  0x21
    cmp  al, 0x0D                ; CR -> end of line
    je   .done
    cmp  al, 0x03                ; Ctrl-C -> cancel, return empty
    je   .ctrlc
    cmp  al, 0x08                ; BS
    je   .bs
    cmp  al, 0x7F                ; DEL (some terminals send this)
    je   .bs
    cmp  al, ' '                 ; ignore other control chars
    jb   .loop
    cmp  cx, INPUT_BUF_LEN-1
    jae  .loop                   ; buffer full
    mov  [di], al
    inc  di
    inc  cx
    mov  dl, al
    mov  ah, 0x06
    int  0x21
    jmp  .loop
.bs:
    test cx, cx
    jz   .loop                   ; nothing to erase
    dec  di
    dec  cx
    mov  byte [di], 0
    mov  ah, 0x06
    mov  dl, 0x08
    int  0x21
    mov  ah, 0x06
    mov  dl, ' '
    int  0x21
    mov  ah, 0x06
    mov  dl, 0x08
    int  0x21
    jmp  .loop
.ctrlc:
    mov  ah, 0x06
    mov  dl, '^'
    int  0x21
    mov  ah, 0x06
    mov  dl, 'C'
    int  0x21
    mov  di, input_buf
    xor  cx, cx
.done:
    mov  byte [di], 0
    mov  ah, 0x06
    mov  dl, 0x0D
    int  0x21
    mov  ah, 0x06
    mov  dl, 0x0A
    int  0x21
    mov  al, [input_buf]
    test cx, cx                  ; set ZF for caller
    pop  di
    ret

; print_str — print $-terminated string at CS:DX via AH=09h.
; Saves DS so callers don't need to set it up.
print_str:
    push ds
    push cs
    pop  ds
    mov  ah, 0x09
    int  0x21
    pop  ds
    ret

; -----------------------------------------------------------------
; parse_command — split input_buf into UPPERCASE cmd_buf + tail_ptr.
;
; Walks input_buf: skips leading space/tab, copies the first token
; (delimited by space/tab/NUL) into cmd_buf with upper-casing, then
; skips trailing whitespace to find the start of arguments.
;
; Caller must have DS = CS = USER_SEG.
;
; Exit:  cmd_buf null-terminated; cmd_len = bytes copied (≤ CMD_BUF_LEN);
;        tail_ptr = offset into input_buf where args begin (or the
;        offset of the trailing NUL if no args).
parse_command:
    push ax
    push cx
    push si
    push di
    mov  si, input_buf
    mov  di, cmd_buf
    xor  cx, cx                  ; CX = bytes copied to cmd_buf
.skip_lead:
    mov  al, [si]
    cmp  al, ' '
    je   .adv_lead
    cmp  al, 9
    jne  .copy
.adv_lead:
    inc  si
    jmp  .skip_lead
.copy:
    mov  al, [si]
    test al, al
    jz   .term
    cmp  al, ' '
    je   .term
    cmp  al, 9
    je   .term
    cmp  cx, CMD_BUF_LEN
    jae  .advance_only           ; truncate but keep walking
    cmp  al, 'a'
    jb   .store
    cmp  al, 'z'
    ja   .store
    sub  al, 0x20
.store:
    mov  [di], al
    inc  di
    inc  cx
.advance_only:
    inc  si
    jmp  .copy
.term:
    mov  byte [di], 0
    mov  [cmd_len], cx
.skip_trail:
    mov  al, [si]
    test al, al
    jz   .save_tail
    cmp  al, ' '
    je   .adv_trail
    cmp  al, 9
    jne  .save_tail
.adv_trail:
    inc  si
    jmp  .skip_trail
.save_tail:
    mov  [tail_ptr], si
    pop  di
    pop  si
    pop  cx
    pop  ax
    ret

; -----------------------------------------------------------------
; name_to_fcb — pack DS:SI 8.3 filename into ES:DI 36-byte FCB.
;
; FCB layout produced:
;   byte 0       drive (0 = default; drive prefix not yet supported)
;   bytes 1..8   name, uppercase, space-padded
;   bytes 9..11  extension, uppercase, space-padded
;   bytes 12..35 zero (kernel fills these on OPEN/CREATE)
;
; Reads up to 8 name chars then optional '.' + 3 ext chars. Excess
; characters in either field are skipped silently. Terminators are
; space, tab, NUL, CR, and comma.
;
; SI is advanced past the entire name token. ES:DI is preserved.
; CF=1 if no name was found (empty / pure-whitespace input).
name_to_fcb:
    push ax
    push cx
    push di

    ; Zero 36 bytes of FCB starting at ES:DI.
    push di
    mov  cx, 18
    xor  ax, ax
    rep  stosw
    pop  di

    ; Skip leading whitespace
.skip_ws:
    mov  al, [si]
    cmp  al, ' '
    je   .adv_ws
    cmp  al, 9
    jne  .got_first
.adv_ws:
    inc  si
    jmp  .skip_ws
.got_first:
    test al, al
    jz   .empty
    cmp  al, 0x0D
    je   .empty
    cmp  al, ','
    je   .empty
    cmp  al, '.'
    je   .empty                  ; bare ".COM" with no name is invalid

    ; Copy up to 8 name chars into FCB[1..8].
    inc  di                      ; past drive byte (already zero)
    mov  cx, 8
.copy_name:
    mov  al, [si]
    cmp  al, '.'
    je   .pad_name_then_ext
    test al, al
    jz   .pad_name
    cmp  al, ' '
    je   .pad_name
    cmp  al, 9
    je   .pad_name
    cmp  al, 0x0D
    je   .pad_name
    cmp  al, ','
    je   .pad_name
    inc  si
    cmp  al, 'a'
    jb   .ns
    cmp  al, 'z'
    ja   .ns
    sub  al, 0x20
.ns:
    mov  [di], al
    inc  di
    loop .copy_name
    ; Name field full. Skip excess until '.' or terminator.
.skip_excess_name:
    mov  al, [si]
    cmp  al, '.'
    je   .consume_dot
    test al, al
    jz   .ext_blank
    cmp  al, ' '
    je   .ext_blank
    cmp  al, 9
    je   .ext_blank
    cmp  al, 0x0D
    je   .ext_blank
    cmp  al, ','
    je   .ext_blank
    inc  si
    jmp  .skip_excess_name

.pad_name:
    ; Pad remaining name slots; no extension follows.
    mov  al, ' '
    rep  stosb
    jmp  .ext_blank

.pad_name_then_ext:
    mov  al, ' '
    rep  stosb
.consume_dot:
    inc  si                      ; consume '.'
    mov  cx, 3
.copy_ext:
    mov  al, [si]
    test al, al
    jz   .pad_ext
    cmp  al, ' '
    je   .pad_ext
    cmp  al, 9
    je   .pad_ext
    cmp  al, 0x0D
    je   .pad_ext
    cmp  al, ','
    je   .pad_ext
    inc  si
    cmp  al, 'a'
    jb   .es
    cmp  al, 'z'
    ja   .es
    sub  al, 0x20
.es:
    mov  [di], al
    inc  di
    loop .copy_ext
    ; Ext field full. Skip excess until terminator.
.skip_excess_ext:
    mov  al, [si]
    test al, al
    jz   .ok
    cmp  al, ' '
    je   .ok
    cmp  al, 9
    je   .ok
    cmp  al, 0x0D
    je   .ok
    cmp  al, ','
    je   .ok
    inc  si
    jmp  .skip_excess_ext

.pad_ext:
    mov  al, ' '
    rep  stosb
    jmp  .ok

.ext_blank:
    mov  cx, 3
    mov  al, ' '
    rep  stosb
.ok:
    clc
    pop  di
    pop  cx
    pop  ax
    ret

.empty:
    stc
    pop  di
    pop  cx
    pop  ax
    ret

; -----------------------------------------------------------------
; dispatch — look up cmd_buf in command_table, run handler on match.
;
; Caller must have DS = CS and have called parse_command first.
;
; Walks command_table {name_ptr, handler_ptr} pairs (terminated by
; {0, 0}). On match, calls the handler. If the handler returns,
; dispatch returns with CF=1. If no entry matches, returns with CF=0.
; A handler may also jmp out of the loop (e.g. to quit); dispatch
; never returns in that case.
dispatch:
    cmp  word [cmd_len], 0
    je   .miss
    push bx
    push si
    push di
    mov  bx, command_table
.walk:
    mov  si, [bx]                ; name pointer
    test si, si
    jz   .end_walk
    mov  di, cmd_buf
.cmp_loop:
    mov  al, [si]
    mov  ah, [di]
    cmp  al, ah
    jne  .next
    test al, al
    jz   .match
    inc  si
    inc  di
    jmp  .cmp_loop
.next:
    add  bx, 4
    jmp  .walk
.end_walk:
    pop  di
    pop  si
    pop  bx
.miss:
    clc
    ret
.match:
    mov  ax, [bx + 2]            ; handler offset
    pop  di
    pop  si
    pop  bx
    call ax                      ; near indirect call
    stc
    ret

; -----------------------------------------------------------------
; external_load — load and run a .COM file by name.
;
; Caller: DS = ES = CS = USER_SEG; SI = name string (terminated by
; space/NUL/CR/comma).
;
; Pipeline:
;   1. name_to_fcb -> fcb_buf (default extension defaults to .COM if
;      the user typed just a bare name like "HELLO").
;   2. INT 21h call 0Fh (OPEN). Failure -> ret CF=1.
;   3. Install on_child_exit as IVT[20h] so the child's INT 20h
;      re-enters the shell loop after the child finishes.
;   4. SETDMA + SEQRD loop, stepping the DTA 128 bytes per record so
;      the .COM image accumulates contiguously at CHILD_SEG:0x100.
;   5. Set DS=ES=SS=CHILD_SEG, SP=0xFFFE, JMP CHILD_SEG:0x100.
;
; CF=1 return = invalid name / OPEN failed / segment overflow.
; A successful launch never returns from here directly; on_child_exit
; lands back in shell_loop after the child's INT 20h.
external_load:
    push si
    mov  di, fcb_buf
    call name_to_fcb
    pop  si
    jc   .fail                   ; empty / invalid name

    ; Default extension to .COM if user typed just "HELLO".
    mov  di, fcb_buf + 9
    cmp  byte [di], ' '
    jne  .ext_set
    mov  byte [di],     'C'
    mov  byte [di + 1], 'O'
    mov  byte [di + 2], 'M'
.ext_set:

    ; OPEN the file. AL=0 success, anything else = failure.
    mov  dx, fcb_buf
    mov  ah, 0x0F
    int  0x21
    test al, al
    jnz  .fail

    ; OPEN succeeded. Save SP for re-entry and arm on_child_exit.
    mov  [shell_sp_save], sp
    cli
    xor  ax, ax
    mov  es, ax
    mov  word [es:INT20_VEC],     on_child_exit
    mov  ax, cs
    mov  word [es:INT20_VEC + 2], ax
    sti
    push cs
    pop  es                      ; restore ES = CS for fcb_buf access

    ; Stream records via a moving DTA. The kernel's SEQRD writes one
    ; 128-byte record per call to the DMA address set by SETDMA, but
    ; doesn't auto-advance it — so we step the DTA forward by 128
    ; ourselves between calls. AL return: 0=full record, 1=partial
    ; last record (EOF), 2=segment wrap (fail), 3=EOF (no data).
    mov  word [dma_offset], 0x100
.read_loop:
    mov  ax, CHILD_SEG
    push ds
    mov  ds, ax
    mov  dx, [cs:dma_offset]
    mov  ah, 0x1A
    int  0x21
    pop  ds

    mov  dx, fcb_buf
    mov  ah, 0x14
    int  0x21
    cmp  al, 1
    je   .read_done
    cmp  al, 3
    je   .read_done
    test al, al
    jnz  .fail                   ; AL=2 segment overflow
    add  word [dma_offset], 128
    jmp  .read_loop

.read_done:
    ; Stand up the child like a fresh transient and JMP.
    mov  ax, CHILD_SEG
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0xFFFE
    jmp  word CHILD_SEG:0x100

.fail:
    stc
    ret

; -----------------------------------------------------------------
; do_exit — EXIT command handler. Tail-jumps to quit, which restores
; the loader's IVT[20h] and INT 20h's into the bootstub halt loop.
do_exit:
    jmp  quit

banner_str:
    db 0x0D, 0x0A
    db 'espDos - 86-DOS Version 1.00', 0x0D, 0x0A
    db 0x0D, 0x0A, '$'

prompt_str:
    db 'A>$'

between:
    db 0x0D, 0x0A, '$'

bad_cmd_msg:
    db 'Bad command or filename', 0x0D, 0x0A, '$'

prev_int20_off:    dw 0
prev_int20_seg:    dw 0
shell_sp_save:     dw 0
dma_offset:        dw 0          ; running DTA for SEQRD-into-CHILD_SEG

; FCB working buffer for OPEN/SEQRD/etc. 36 bytes covers the standard
; layout (drive(1) + name(8) + ext(3) + reserved(20) + current_record(1)
; with slack).
fcb_buf:           times 36 db 0

; Static input buffer for read_line. Sized to DOS-classic line length
; plus one for the null terminator.
input_buf:         times INPUT_BUF_LEN+1 db 0

; parse_command outputs: uppercased command, its length, and a
; pointer (offset within input_buf) to where args begin.
cmd_buf:           times CMD_BUF_LEN+1 db 0
cmd_len:           dw 0
tail_ptr:          dw 0

; Built-in command table — {name_ptr, handler_ptr} pairs, {0,0} terminated.
; Names are uppercase, NUL-terminated, matched case-sensitively against
; cmd_buf (which parse_command upper-cases on entry).
command_table:
    dw  cmd_exit_str, do_exit
    dw  0, 0

cmd_exit_str:      db 'EXIT', 0
