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
cpu 8086                            ; refuse 286+ encodings — 8086tiny
                                    ; sees `0F 8x` as POP CS + junk and
                                    ; wanders into uninitialized memory
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
    ; DS is still the child's segment here, so shell_sp_save must be
    ; read with a CS-override before we restore DS=CS=USER_SEG.
    cli
    mov  ax, cs
    mov  ss, ax
    mov  sp, [cs:shell_sp_save]
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
;   1. name_to_fcb -> fcb_buf (default extension to .COM).
;   2. SETDMA + SRCHFRST so the kernel writes the matching directory
;      entry into dir_buf. AL != 0 -> file not found -> CF=1.
;   3. Read starting cluster (dir_buf+27) and size low word
;      (dir_buf+29) from the FCB-format directory entry. Translate
;      cluster N -> sector N+9 (build_disk.py allocates each file as
;      a run of contiguous clusters in the data area starting at
;      sector 11), and size -> sector count = (size+511)/512.
;   4. Install on_child_exit as IVT[20h] so the child's INT 20h
;      re-enters the shell loop after the child finishes.
;   5. BIOSREAD the sector run directly into CHILD_SEG:0x100 via
;      far-call to BIOSSEG:0x0015. We bypass INT 21h SEQRD because
;      the kernel's LOAD path divides by [BP+SECSIZ] and the DPB it
;      computes for drive 0 in our minimal init has SECSIZ=0; the
;      divide traps to IVT[0] which is unpopulated, halting the
;      emulator at CS:IP=0:0. Same workaround loader.asm uses.
;   6. Set DS=ES=SS=CHILD_SEG, SP=0xFFFE, JMP CHILD_SEG:0x100.
;
; CF=1 return = invalid name / file not found / zero-byte file /
;               BIOSREAD failure (with IVT[20h] restored).
; A successful launch never returns from here directly;
; on_child_exit lands back in shell_loop after the child's INT 20h.
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

    ; SETDMA(DS:DX = dir_buf). DS = USER_SEG already.
    mov  dx, dir_buf
    mov  ah, 0x1A
    int  0x21

    ; SRCHFRST with the exact filename — kernel writes the directory
    ; entry into dir_buf. AL=0 success, AL=0xFF not found.
    mov  dx, fcb_buf
    mov  ah, 0x11
    int  0x21
    test al, al
    jnz  .fail

    ; cluster + 9 = data sector (cluster 2 -> sector 11).
    mov  ax, [dir_buf + 27]
    add  ax, 9
    mov  word [load_sector], ax

    ; (size + 511) / 512 = sector count. 16-bit math is enough; our
    ; largest transient is ~2 KB and FAT12 caps the disk at 16 MB
    ; anyway. Zero-byte files have no sectors and are rejected.
    mov  ax, [dir_buf + 29]
    test ax, ax
    jz   .fail
    add  ax, 511
    mov  cl, 9
    shr  ax, cl
    mov  word [load_count], ax

    ; Save SP and arm on_child_exit before BIOSREAD. If BIOSREAD
    ; fails we restore IVT[20h] in .fail_postivt below.
    mov  [shell_sp_save], sp
    cli
    xor  ax, ax
    mov  es, ax
    mov  word [es:INT20_VEC],     on_child_exit
    mov  ax, cs
    mov  word [es:INT20_VEC + 2], ax
    sti

    ; BIOSREAD: AL=drive, DS:BX=destination buffer, CX=sector count,
    ; DX=starting sector. The far call's CS = BIOSSEG triggers the
    ; emulator's per-step BIOS-call trap; bios.c services it and
    ; RETF's back here.
    mov  cx, [load_count]        ; while DS = USER_SEG
    mov  dx, [load_sector]
    mov  ax, CHILD_SEG
    mov  ds, ax
    mov  bx, 0x100
    mov  al, 0                   ; drive A (AH ignored by BIOSREAD)
    call 0x0040:0x0015
    jc   .fail_postivt

    ; Stand up the child like a fresh transient and JMP.
    mov  ax, CHILD_SEG
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0xFFFE
    jmp  word CHILD_SEG:0x100

.fail_postivt:
    ; BIOSREAD failed. Restore loader's IVT[20h] = halt before
    ; reporting failure so a subsequent EXIT halts cleanly.
    cli
    xor  ax, ax
    mov  es, ax
    push cs
    pop  ds
    mov  ax, [prev_int20_off]
    mov  word [es:INT20_VEC], ax
    mov  ax, [prev_int20_seg]
    mov  word [es:INT20_VEC + 2], ax
    sti
    push cs
    pop  es
    stc
    ret

.fail:
    stc
    ret

; -----------------------------------------------------------------
; Built-in command handlers. Each is invoked via `call ax` from
; dispatch with DS = ES = CS. Returning lets dispatch tag CF=1 so
; the caller re-prompts. Handlers that don't want to return (EXIT)
; jmp to quit / shell_loop instead.

; EXIT — tail-jump to quit; restores loader IVT[20h] + INT 20h halts.
do_exit:
    jmp  quit

; CLS — ANSI: ESC[2J ESC[H. Both the USB-Serial-JTAG terminal and the
; C5 display_log component understand these escapes.
do_cls:
    mov  dx, cls_str
    jmp  print_str

; VER — print the version banner.
do_ver:
    mov  dx, ver_str
    jmp  print_str

; DATE / TIME — kernel dispatch only goes up to function 41 (MAKEFCB);
; the date/time calls (2Ah-2Dh) landed in 86-DOS 1.10. Print a note
; so users understand the limitation rather than guessing why.
do_date_time:
    mov  dx, date_time_str
    jmp  print_str

; DIR — list every file in the root directory.
;
; Pipeline:
;   1. Build a wildcard FCB at fcb_buf (drive=0, name+ext = 11 '?').
;   2. SETDMA -> dir_buf so the kernel writes the matching entry there.
;   3. SRCHFRST (call 11h); on AL=0xFF print "File not found".
;   4. Print row: 8-char name + ' ' + 3-char ext + spaces + size + CRLF.
;   5. SRCHNXT (call 12h) until AL=0xFF.
;
; Args after DIR are ignored for now — the wildcard pattern always
; matches everything. Pattern parsing (DIR *.COM, DIR FOO) is a
; future iteration.
do_dir:
    push si
    push di

    ; Build wildcard FCB (drive=0, 11 '?' across name+ext, rest zero).
    mov  di, fcb_buf
    push di
    mov  cx, 18
    xor  ax, ax
    rep  stosw                   ; zero 36 bytes
    pop  di
    inc  di                      ; skip drive byte (already 0)
    mov  cx, 11
    mov  al, '?'
    rep  stosb

    ; SETDMA(DS:DX = dir_buf). DS is already CS=USER_SEG.
    mov  dx, dir_buf
    mov  ah, 0x1A
    int  0x21

    ; SRCHFRST.
    mov  dx, fcb_buf
    mov  ah, 0x11
    int  0x21
    test al, al
    jnz  .none

.list_loop:
    ; 86-DOS 1.0 SRCHFRST/SRCHNXT writes an FCB-format record to the
    ; DTA, not the raw on-disk FAT12 entry. Layout we observe:
    ;   byte 0      drive (0 = A)
    ;   bytes 1..8  name (space-padded, uppercase)
    ;   bytes 9..11 ext  (space-padded, uppercase)
    ;   bytes 29..30 file size, low 16 bits (our files are < 64KB)
    ; Filter empty (0x00 -> end of dir) and deleted (0xE5) slots —
    ; the kernel's '?' wildcard matches everything including those.
    mov  al, [dir_buf + 1]
    test al, al
    jz   .end
    cmp  al, 0xE5
    je   .next

    ; Print 8-char name from dir_buf[1..8].
    mov  cx, 8
    mov  si, dir_buf + 1
.name_out:
    mov  dl, [si]
    inc  si
    mov  ah, 0x06
    int  0x21
    loop .name_out

    mov  dl, ' '
    mov  ah, 0x06
    int  0x21

    ; Print 3-char ext from dir_buf[9..11].
    mov  cx, 3
    mov  si, dir_buf + 9
.ext_out:
    mov  dl, [si]
    inc  si
    mov  ah, 0x06
    int  0x21
    loop .ext_out

    ; Two spaces before size.
    mov  dl, ' '
    mov  ah, 0x06
    int  0x21
    mov  dl, ' '
    mov  ah, 0x06
    int  0x21

    ; Size low word at dir_buf[29..30].
    mov  ax, [dir_buf + 29]
    call print_dec

    ; CRLF.
    mov  dl, 0x0D
    mov  ah, 0x06
    int  0x21
    mov  dl, 0x0A
    mov  ah, 0x06
    int  0x21

.next:
    ; SRCHNXT.
    mov  dx, fcb_buf
    mov  ah, 0x12
    int  0x21
    test al, al
    jz   .list_loop

.end:
    pop  di
    pop  si
    ret

.none:
    mov  dx, file_not_found_msg
    call print_str
    pop  di
    pop  si
    ret

; DEL / ERASE — remove a file from the directory. INT 21h call 13h.
;
; Pipeline: name_to_fcb on tail_ptr -> fcb_buf, then DELETE.
; AL=0 success, AL=0xFF if no file matched the FCB pattern (note:
; kernel's DELETE supports wildcards; we don't filter them out).
do_del:
    push si
    push di
    mov  si, [tail_ptr]
    mov  di, fcb_buf
    call name_to_fcb
    jc   .no_arg
    mov  dx, fcb_buf
    mov  ah, 0x13
    int  0x21
    test al, al
    jnz  .not_found
    pop  di
    pop  si
    ret
.not_found:
    mov  dx, file_not_found_msg
    call print_str
    pop  di
    pop  si
    ret
.no_arg:
    mov  dx, no_arg_msg
    call print_str
    pop  di
    pop  si
    ret

; REN — rename old to new. INT 21h call 17h.
;
; Kernel expects a "rename FCB": old name at fcb_buf[0..11], new name
; at fcb_buf[16..27]. We call name_to_fcb twice — first on fcb_buf for
; the old name (advances SI past it), then on fcb_buf+16 for the new
; name. name_to_fcb zeroes 36 bytes per call, so the two calls together
; touch fcb_buf[0..51]; that's why fcb_buf is sized 64.
do_ren:
    push si
    push di
    mov  si, [tail_ptr]
    mov  di, fcb_buf
    call name_to_fcb
    jc   .no_arg
    mov  di, fcb_buf + 16
    call name_to_fcb
    jc   .no_arg
    mov  dx, fcb_buf
    mov  ah, 0x17
    int  0x21
    test al, al
    jnz  .not_found
    pop  di
    pop  si
    ret
.not_found:
    mov  dx, file_not_found_msg
    call print_str
    pop  di
    pop  si
    ret
.no_arg:
    mov  dx, no_arg_msg
    call print_str
    pop  di
    pop  si
    ret

; TYPE — read a file and dump its bytes to the console.
;
; Pipeline:
;   1. name_to_fcb on tail_ptr -> fcb_buf.
;   2. OPEN (call 0Fh). On AL!=0 print "File not found".
;   3. SETDMA(type_buf, 128 bytes) once.
;   4. Loop SEQRD (call 14h). For each record (full or partial),
;      output bytes via AH=02h (CONOUT). Stop at first 0x1A
;      (Ctrl-Z = period-correct DOS EOF marker) or when SEQRD
;      reports AL=1 (partial last record) / AL=3 (EOF, no data).
;   5. We use AH=02h instead of AH=06h here because RAWIO interprets
;      DL=0xFF as "input request"; TYPE on a binary file hits 0xFF
;      bytes legitimately and AH=02h outputs them verbatim.
do_type:
    push si
    push di
    mov  si, [tail_ptr]
    mov  di, fcb_buf
    call name_to_fcb
    jc   .no_arg

    ; OPEN.
    mov  dx, fcb_buf
    mov  ah, 0x0F
    int  0x21
    test al, al
    jnz  .not_found

    ; SETDMA -> type_buf.
    mov  dx, type_buf
    mov  ah, 0x1A
    int  0x21

.read_loop:
    mov  dx, fcb_buf
    mov  ah, 0x14
    int  0x21
    cmp  al, 1
    je   .last_record
    cmp  al, 3
    je   .done
    test al, al
    jnz  .done                   ; AL=2 segment overflow — stop

    ; Full 128-byte record. Output until first 0x1A.
    mov  cx, 128
    mov  si, type_buf
.full_loop:
    mov  dl, [si]
    cmp  dl, 0x1A
    je   .done
    inc  si
    mov  ah, 0x02
    int  0x21
    loop .full_loop
    jmp  .read_loop

.last_record:
    ; Partial record — kernel zero-fills past EOF. Output until first
    ; 0x1A or NUL.
    mov  cx, 128
    mov  si, type_buf
.partial_loop:
    mov  dl, [si]
    cmp  dl, 0x1A
    je   .done
    test dl, dl
    jz   .done
    inc  si
    mov  ah, 0x02
    int  0x21
    loop .partial_loop

.done:
    pop  di
    pop  si
    ret

.not_found:
    mov  dx, file_not_found_msg
    call print_str
    pop  di
    pop  si
    ret

.no_arg:
    mov  dx, no_arg_msg
    call print_str
    pop  di
    pop  si
    ret

; COPY — read src, create dst, stream records from src to dst.
;
; Pipeline:
;   1. name_to_fcb(src) -> fcb_buf, name_to_fcb(dst) -> dst_fcb_buf
;   2. OPEN src; CREATE dst (truncates existing dst silently — same as
;      classic DOS COPY).
;   3. SETDMA -> type_buf (one fixed 128-byte staging buffer).
;   4. Loop: SEQRD(src) writes 128 bytes into type_buf; SEQWRT(dst)
;      reads them back out from type_buf and writes to disk.
;      AL=1 (partial last record from src) -> still SEQWRT the full
;      128 bytes (dst rounds up to record-size, like real DOS COPY
;      with default RECSIZ=128); then stop.
;      AL=3 (EOF, no data) -> stop.
;   5. CLOSE dst to flush its directory entry.
do_copy:
    push si
    push di

    ; Parse src into fcb_buf.
    mov  si, [tail_ptr]
    mov  di, fcb_buf
    call name_to_fcb
    jc   .no_arg

    ; Parse dst into dst_fcb_buf (SI is past src after first call).
    mov  di, dst_fcb_buf
    call name_to_fcb
    jc   .no_arg

    ; OPEN src.
    mov  dx, fcb_buf
    mov  ah, 0x0F
    int  0x21
    test al, al
    jnz  .not_found

    ; CREATE dst (truncates if exists; AL=0xFF if dir full).
    mov  dx, dst_fcb_buf
    mov  ah, 0x16
    int  0x21
    test al, al
    jnz  .create_failed

    ; SETDMA -> type_buf for both reads and writes.
    mov  dx, type_buf
    mov  ah, 0x1A
    int  0x21

.read_loop:
    mov  dx, fcb_buf
    mov  ah, 0x14
    int  0x21
    cmp  al, 1
    je   .last
    cmp  al, 3
    je   .close_dst
    test al, al
    jnz  .copy_failed            ; AL=2 segment overflow

    ; Full record copied. SEQWRT to dst.
    mov  dx, dst_fcb_buf
    mov  ah, 0x15
    int  0x21
    test al, al
    jnz  .copy_failed
    jmp  .read_loop

.last:
    ; Partial last record. Write the full 128 bytes anyway —
    ; the kernel zero-pads past EOF, which is the documented
    ; rounding-up behavior for default-RECSIZ COPY.
    mov  dx, dst_fcb_buf
    mov  ah, 0x15
    int  0x21

.close_dst:
    ; CLOSE dst so its directory entry is flushed.
    mov  dx, dst_fcb_buf
    mov  ah, 0x10
    int  0x21
    pop  di
    pop  si
    ret

.not_found:
    mov  dx, file_not_found_msg
    call print_str
    pop  di
    pop  si
    ret

.create_failed:
.copy_failed:
    mov  dx, copy_failed_msg
    call print_str
    pop  di
    pop  si
    ret

.no_arg:
    mov  dx, no_arg_msg
    call print_str
    pop  di
    pop  si
    ret

; print_dec — print AX as unsigned decimal, right-aligned in 6 cols
; (uses dec_buf as a scratch-then-print buffer).
print_dec:
    push ax
    push bx
    push cx
    push dx
    ; Pre-fill dec_buf with 6 spaces (terminator at offset 6 stays).
    mov  byte [dec_buf + 0], ' '
    mov  byte [dec_buf + 1], ' '
    mov  byte [dec_buf + 2], ' '
    mov  byte [dec_buf + 3], ' '
    mov  byte [dec_buf + 4], ' '
    mov  byte [dec_buf + 5], ' '
    mov  bx, dec_buf + 5         ; rightmost slot
    test ax, ax
    jnz  .div_loop
    mov  byte [bx], '0'
    jmp  .out
.div_loop:
    test ax, ax
    jz   .out
    xor  dx, dx
    mov  cx, 10
    div  cx                      ; AX /= 10, DX = digit
    add  dl, '0'
    mov  [bx], dl
    dec  bx
    jmp  .div_loop
.out:
    mov  dx, dec_buf
    call print_str
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

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

cls_str:
    db 0x1B, '[2J', 0x1B, '[H', '$'

ver_str:
    db 'espDos - 86-DOS Version 1.00', 0x0D, 0x0A, '$'

date_time_str:
    db 'Not supported in 86-DOS 1.00 (added in 1.10)', 0x0D, 0x0A, '$'

file_not_found_msg:
    db 'File not found', 0x0D, 0x0A, '$'

no_arg_msg:
    db 'Required parameter missing', 0x0D, 0x0A, '$'

copy_failed_msg:
    db 'Copy failed', 0x0D, 0x0A, '$'

dec_buf:
    db '      $'                  ; 6 spaces + $; print_dec fills digits

prev_int20_off:    dw 0
prev_int20_seg:    dw 0
shell_sp_save:     dw 0
load_sector:       dw 0          ; first data sector for the .COM file
load_count:        dw 0          ; number of contiguous sectors to read

; FCB working buffer for OPEN/SEQRD/etc. 64 bytes covers the standard
; layout (drive(1) + name(8) + ext(3) + reserved(20) + current_record(1))
; *plus* the dual-FCB layout RENAME needs (old name at 0..11, new name
; at 16..27) — name_to_fcb's per-call 36-byte zero overlaps from offset
; 16 to offset 51, so fcb_buf has to be at least 52 bytes; 64 gives
; slack for any kernel-internal use we missed.
fcb_buf:           times 64 db 0

; DTA scratch for SRCHFRST/SRCHNXT — kernel writes one 32-byte
; directory entry here per match (name(8), ext(3), attr(1), reserved(10),
; time(2), date(2), cluster(2), size(4)).
dir_buf:           times 32 db 0

; SEQRD scratch for TYPE / COPY — one 128-byte record per call.
type_buf:          times 128 db 0

; Second FCB for COPY's destination — 36 bytes is the standard
; opened-FCB size. (RENAME packs both names into one fcb_buf via
; the 0/16 offset trick; COPY can't because src and dst are
; independently OPEN/CREATE'd and each maintains its own NEXTREC.)
dst_fcb_buf:       times 36 db 0

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
    dw  cmd_cls_str,  do_cls
    dw  cmd_ver_str,  do_ver
    dw  cmd_date_str, do_date_time
    dw  cmd_time_str, do_date_time
    dw  cmd_dir_str,   do_dir
    dw  cmd_type_str,  do_type
    dw  cmd_del_str,   do_del
    dw  cmd_erase_str, do_del         ; ERASE = alias for DEL
    dw  cmd_ren_str,   do_ren
    dw  cmd_copy_str,  do_copy
    dw  0, 0

cmd_exit_str:      db 'EXIT', 0
cmd_cls_str:       db 'CLS', 0
cmd_ver_str:       db 'VER', 0
cmd_date_str:      db 'DATE', 0
cmd_time_str:      db 'TIME', 0
cmd_dir_str:       db 'DIR', 0
cmd_type_str:      db 'TYPE', 0
cmd_del_str:       db 'DEL', 0
cmd_erase_str:     db 'ERASE', 0
cmd_ren_str:       db 'REN', 0
cmd_copy_str:      db 'COPY', 0
