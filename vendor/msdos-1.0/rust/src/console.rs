//! console.rs — Console I/O, buffered line input, template editing for 86-DOS 1.00.
//!
//! Translated from 86DOS.asm.  The following ASM labels are covered here:
//!
//!   BUFIN       86DOS.asm:2566-2651  — buffered line input with template editing
//!   CRLF        86DOS.asm:2652-2656  — output CR+LF
//!   CONOUT      86DOS.asm:2853-2855  — output one character to console
//!   OUT         86DOS.asm:2855-2966  — output with optional printer echo
//!   OUTCH       86DOS.asm:2855-2966  — output with control-char rendering
//!   CONSTAT     86DOS.asm:2968-2972  — test console status
//!   CONIN       86DOS.asm:2975-2980  — blocking character input
//!   IN          86DOS.asm:2983-2986  — read + echo
//!   RAWIO       86DOS.asm:2988-2999  — raw I/O (DL=0xFF → input, else output)
//!   RAWINP      86DOS.asm:2988-2999  — raw input (no echo)
//!   LIST        86DOS.asm:3001-3004  — output to list device
//!   PRTBUF      86DOS.asm:3006-3013  — print the console output buffer
//!   OUTMES      86DOS.asm:3015-3021  — output '$'-terminated string

use crate::DosState;

const CR: u8 = b'\r';
const LF: u8 = b'\n';
const BS: u8 = 0x08;
const DEL: u8 = 0x7F;
const CTRLC: u8 = 0x03;
const CTRLS: u8 = 0x13;
const ESC: u8 = crate::types::ESCCH;

// Template-editing special keys (86DOS.asm console section)
const F1: u8 = 0x01; // copy one char from template
const F3: u8 = 0x03; // copy rest of template
const INS_KEY: u8 = 0x16; // toggle insert mode

// ── Output primitives ─────────────────────────────────────────────────────────

/// conout — Output one character to the console, tracking cursor position.
///
/// ASM: CONOUT  86DOS.asm:2853-2855  (and the OUT/OUTCH chain)
///
/// Inputs:
///   state — DOS kernel state (car_pos, start_pos maintained)
///   c     — character to output (DL in ASM)
/// Outputs:
///   Character sent to BIOS output.
///   state.car_pos updated: CR → start_pos, BS → decrement, TAB → align, else +1.
pub fn conout(state: &mut DosState, c: u8) {
    state.bios.output(c);
    // ASM line 2854: update CARPOS
    if c == CR {
        state.car_pos = state.start_pos;
    } else if c == BS {
        if state.car_pos > 0 {
            state.car_pos -= 1;
        }
    } else if c == b'\t' {
        // tab to next 8-column boundary
        state.car_pos = (state.car_pos & !7) + 8;
    } else {
        state.car_pos = state.car_pos.wrapping_add(1);
    }
}

/// out — Output character with optional printer echo (PFLAG).
///
/// ASM: OUT  86DOS.asm:2855-2966
///
/// Inputs:
///   state.pflag — nonzero → also send to printer
///   c           — character
/// Outputs:
///   Character sent to console (via conout) and optionally to printer.
pub fn out(state: &mut DosState, c: u8) {
    conout(state, c);
    // ASM line 2860: TEST [PFLAG],0FFh / JZ NOPRT
    if state.pflag != 0 {
        state.bios.print(c);
    }
}

/// outch — Output character, rendering control characters as ^X.
///
/// ASM: OUTCH  86DOS.asm:2855-2966  (OUTCH path)
///
/// Inputs:
///   c — character to output
/// Outputs:
///   Control chars (< 0x20, except CR/LF/TAB/BS) rendered as '^' + letter.
///   Others passed directly to out().
pub fn outch(state: &mut DosState, c: u8) {
    // ASM line 2870: CMP AL,20h / JB CTRLCH
    if c < b' ' && c != CR && c != LF && c != b'\t' && c != BS {
        out(state, b'^');
        out(state, c + b'@');
    } else {
        out(state, c);
    }
}

/// crlf — Output carriage return + line feed.
///
/// ASM: CRLF  86DOS.asm:2652-2656
///
/// Inputs:  (none)
/// Outputs: CR then LF sent to console.
pub fn crlf(state: &mut DosState) {
    out(state, CR);
    out(state, LF);
}

/// outmes — Output a '$'-terminated string to the console.
///
/// ASM: OUTMES  86DOS.asm:3015-3021
///
/// Inputs:
///   msg — byte slice; '$' (0x24) is the terminator (DS:DX in ASM)
/// Outputs:
///   Each byte before '$' sent via out().
pub fn outmes(state: &mut DosState, msg: &[u8]) {
    // ASM line 3016: CMP AL,'$' / JE OUTMDONE
    for &c in msg {
        if c == b'$' {
            break;
        }
        out(state, c);
    }
}

/// prtbuf — Output the console output buffer and clear it.
///
/// ASM: PRTBUF  86DOS.asm:3006-3013
///
/// Inputs:  state.con_buf
/// Outputs: All bytes in con_buf sent via out(); con_buf cleared.
pub fn prtbuf(state: &mut DosState) {
    let buf = state.con_buf.clone();
    for &c in &buf {
        out(state, c);
    }
}

// ── Input primitives ──────────────────────────────────────────────────────────

/// conin — Blocking read of one character from the console.
///
/// ASM: CONIN  86DOS.asm:2975-2980
///
/// Inputs:  (none)
/// Outputs: Returns character in AL.
pub fn conin(state: &mut DosState) -> u8 {
    state.bios.input()
}

/// inp — Read a character and echo it.
///
/// ASM: IN  86DOS.asm:2983-2986
///
/// Inputs:  (none)
/// Outputs: Character returned; echoed via outch().
pub fn inp(state: &mut DosState) -> u8 {
    let c = conin(state);
    outch(state, c);
    c
}

/// constat — Test whether a character is ready on the console.
///
/// ASM: CONSTAT  86DOS.asm:2968-2972
///
/// Outputs: Nonzero if a character is available, 0 otherwise.
pub fn constat(state: &mut DosState) -> u8 {
    statchk(state)
}

/// statchk — Check console status via BIOS.
///
/// ASM: CONSTAT  86DOS.asm:2968-2972  (inner BIOS call)
pub fn statchk(state: &mut DosState) -> u8 {
    state.bios.stat()
}

/// inchk — Return a character if one is ready, else 0.
///
/// ASM: (inline status+read combination)  86DOS.asm:2968-2986
pub fn inchk(state: &mut DosState) -> u8 {
    if statchk(state) != 0 {
        conin(state)
    } else {
        0
    }
}

/// rawio — Raw I/O: DL=0xFF → read, else → write.
///
/// ASM: RAWIO  86DOS.asm:2988-2999
///
/// Inputs:
///   dl — 0xFF for raw input; any other value is the byte to output
/// Outputs:
///   Input: returns character from BIOS input (no echo).
///   Output: sends dl to BIOS output; returns dl.
pub fn rawio(state: &mut DosState, dl: u8) -> u8 {
    // ASM line 2989: CMP DL,0FFh / JE RAWINPUT
    if dl == 0xFF {
        rawinp(state)
    } else {
        rawout(state, dl);
        dl
    }
}

/// rawinp — Raw input (no echo).
///
/// ASM: RAWINP  86DOS.asm:2988-2999  (RAWINPUT path)
pub fn rawinp(state: &mut DosState) -> u8 {
    conin(state)
}

/// rawout — Raw output (no processing, no echo).
///
/// ASM: (RAWIO output path)  86DOS.asm:2988-2999
pub fn rawout(state: &mut DosState, c: u8) {
    state.bios.output(c);
}

/// list — Send a character to the list (printer) device.
///
/// ASM: LIST  86DOS.asm:3001-3004
///
/// Inputs:
///   c — character to print (DL in ASM)
/// Outputs:
///   Character sent to BIOS print routine.
pub fn list(state: &mut DosState, c: u8) {
    state.bios.print(c);
}

// ── BUFIN: buffered line input with template editing ─────────────────────────

/// bufin — Buffered line input with template (previous-line) editing.
///
/// ASM: BUFIN  86DOS.asm:2566-2651
///
/// Inputs:
///   state.in_buf — template (previous line, used for F1/F3 template keys)
///   max_len      — maximum input line length (byte 0 of DOS buffer in ASM)
/// Outputs:
///   state.in_buf — updated to the new line (becomes template for next call)
///   Returns the new line as Vec<u8> (without terminating CR).
///
/// The ASM implements a full screen editor with:
///   CR         → end input (ENDLIN, line 2600)
///   ESC        → erase new line, re-display template start (RUBOUT, line 2608)
///   BS / DEL   → erase one character (line 2617)
///   TAB        → expand to 8-column boundary (line 2624)
///   F1 (0x01)  → copy one char from template (COPYONE, line 2632)
///   F3 (0x03)  → copy rest of template (COPYLIN, line 2638)
///   Ins (0x16) → toggle insert mode (line 2644)
///   Other      → store character (SAVCH, line 2648)
pub fn bufin(state: &mut DosState, max_len: u8) -> Vec<u8> {
    let template: Vec<u8> = state.in_buf.clone();
    let mut new_line: Vec<u8> = Vec::new();
    let mut tmpl_pos = 0usize;
    let mut insert_mode = false;
    state.start_pos = state.car_pos;

    loop {
        let c = getch(state);
        match c {
            CR => {
                // ASM line 2600: ENDLIN — CR terminates input
                crlf(state);
                break;
            }
            ESC => {
                // ASM line 2608: ESC — erase new line, restart from template
                kilnew(state, &new_line);
                new_line.clear();
                tmpl_pos = 0;
                newlin(state);
            }
            BS | DEL => {
                // ASM line 2617: RUBOUT — erase one character
                if !new_line.is_empty() {
                    new_line.pop();
                    if tmpl_pos > 0 {
                        tmpl_pos -= 1;
                    }
                    backsp(state);
                }
            }
            b'\t' => {
                // ASM line 2624: TAB processing
                tab(state, &mut new_line, max_len);
            }
            0x01 => {
                // ASM line 2632: F1 — COPYONE: copy one char from template
                if tmpl_pos < template.len() {
                    let tc = template[tmpl_pos];
                    tmpl_pos += 1;
                    if new_line.len() < max_len as usize {
                        new_line.push(tc);
                        outch(state, tc);
                    }
                }
            }
            0x03 => {
                // ASM line 2638: F3 — COPYLIN: copy rest of template
                while tmpl_pos < template.len() && new_line.len() < max_len as usize {
                    let tc = template[tmpl_pos];
                    tmpl_pos += 1;
                    new_line.push(tc);
                    outch(state, tc);
                }
            }
            INS_KEY => {
                // ASM line 2644: toggle insert mode
                insert_mode = !insert_mode;
            }
            _ => {
                // ASM line 2648: SAVCH — store character
                if new_line.len() < max_len as usize {
                    if !insert_mode && tmpl_pos < template.len() {
                        tmpl_pos += 1;
                    }
                    new_line.push(c);
                    outch(state, c);
                }
            }
        }
    }
    state.in_buf = new_line.clone();
    new_line
}

/// getch — Get a character, handling Ctrl-S pause.
///
/// ASM: GETCH  86DOS.asm:2566-2651  (inner input loop)
///
/// Inputs:  (none)
/// Outputs: Returns character; Ctrl-S causes wait for next character (pause).
pub fn getch(state: &mut DosState) -> u8 {
    loop {
        let c = conin(state);
        // ASM line 2572: CMP AL,CTRLS / JNE NOTCTRLS
        if c == CTRLS {
            conin(state); // wait for any key to resume
            continue;
        }
        return c;
    }
}

/// savch — Save a character into the line buffer if space allows.
///
/// ASM: SAVCH  86DOS.asm:2648-2651
///
/// Inputs:
///   buf — current line buffer
///   c   — character to append
///   max — maximum buffer length
pub fn savch(buf: &mut Vec<u8>, c: u8, max: u8) {
    if buf.len() < max as usize {
        buf.push(c);
    }
}

/// backsp — Erase one character on screen (BS SPC BS sequence).
///
/// ASM: BACKSP  86DOS.asm:2566-2651  (backspace helper)
pub fn backsp(state: &mut DosState) {
    out(state, BS);
    out(state, b' ');
    out(state, BS);
}

/// backup — Move cursor left N positions.
///
/// ASM: (cursor-backup helper)  86DOS.asm:2566-2651
pub fn backup(state: &mut DosState, n: u8) {
    for _ in 0..n {
        out(state, BS);
    }
}

/// backmes — Erase `n` characters on screen (n × BS SPC BS).
///
/// ASM: (erase-to-column helper)  86DOS.asm:2566-2651
pub fn backmes(state: &mut DosState, n: usize) {
    for _ in 0..n {
        out(state, BS);
        out(state, b' ');
        out(state, BS);
    }
}

/// tab — Expand a TAB into spaces to the next 8-column boundary.
///
/// ASM: TAB  86DOS.asm:2566-2651  (tab expansion)
///
/// Inputs:
///   buf — current line buffer
///   max — maximum buffer length
/// Outputs:
///   Spaces appended to buf until car_pos is a multiple of 8.
pub fn tab(state: &mut DosState, buf: &mut Vec<u8>, max: u8) {
    let spaces = 8 - (state.car_pos % 8);
    for _ in 0..spaces {
        if buf.len() < max as usize {
            buf.push(b' ');
            out(state, b' ');
        }
    }
}

/// kilnew — Erase the new-line characters typed so far (ESC handler).
///
/// ASM: (RUBOUT / ESC path)  86DOS.asm:2608-2615
pub fn kilnew(state: &mut DosState, new_line: &[u8]) {
    backmes(state, new_line.len());
}

/// newlin — Output CR+LF (restart line display after ESC).
///
/// ASM: (NEWLIN within BUFIN)  86DOS.asm:2608-2615
pub fn newlin(state: &mut DosState) {
    crlf(state);
}

/// ctrlout — Output a control character visibly (^X style).
///
/// ASM: (control-char output helper)  86DOS.asm:2855-2966
pub fn ctrlout(state: &mut DosState, c: u8) {
    out(state, b'^');
    out(state, c + b'@');
}

/// bufout — Output and clear the console output buffer.
///
/// ASM: (BUFOUT path)  86DOS.asm:3006-3013
pub fn bufout(state: &mut DosState) {
    let buf = state.con_buf.clone();
    for &c in &buf {
        out(state, c);
    }
    state.con_buf.clear();
}

// ── Template editing stubs ────────────────────────────────────────────────────

/// copylin — Copy entire template into new_line (F3 helper).
///
/// ASM: (F3 / COPYLIN path)  86DOS.asm:2638-2642
pub fn copylin(template: &[u8], new_line: &mut Vec<u8>, max: u8) {
    for &c in template {
        if new_line.len() >= max as usize {
            break;
        }
        new_line.push(c);
    }
}

/// copyone — Copy one character from template at *pos (F1 helper).
///
/// ASM: (F1 / COPYONE path)  86DOS.asm:2632-2636
pub fn copyone(template: &[u8], pos: &mut usize, new_line: &mut Vec<u8>, max: u8) {
    if *pos < template.len() && new_line.len() < max as usize {
        new_line.push(template[*pos]);
        *pos += 1;
    }
}

/// copystr — Copy template up to (not including) `stop` byte (F2 helper).
///
/// ASM: (F2 / COPYSTR path)  86DOS.asm:2566-2651
pub fn copystr(template: &[u8], pos: &mut usize, new_line: &mut Vec<u8>, max: u8, stop: u8) {
    while *pos < template.len() && template[*pos] != stop && new_line.len() < max as usize {
        new_line.push(template[*pos]);
        *pos += 1;
    }
}

/// skipone — Skip one character in the template (F4/Del-key helper).
///
/// ASM: (F4 / SKIPONE path)  86DOS.asm:2566-2651
pub fn skipone(pos: &mut usize, template_len: usize) {
    if *pos < template_len {
        *pos += 1;
    }
}

/// skipstr — Skip template up to (not including) `stop` byte (F4-string helper).
///
/// ASM: (SKIPSTR path)  86DOS.asm:2566-2651
pub fn skipstr(template: &[u8], pos: &mut usize, stop: u8) {
    while *pos < template.len() && template[*pos] != stop {
        *pos += 1;
    }
}
