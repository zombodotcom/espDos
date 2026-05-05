/*
 * console.c — Console I/O: character input/output, buffered line input,
 *             Ctrl-C handling, status check.
 *
 * Translated from 86DOS.asm.  ASM labels covered:
 *
 *   CONIN       86DOS.asm:2975-2980  — system call 1:  console input with echo
 *   CONOUT      86DOS.asm:2853-2934  — system call 2:  console output
 *   OUT         86DOS.asm:2855-2934  — internal console output (incl. Ctrl tracking)
 *   BUFOUT      86DOS.asm:2841-2851  — output char with ^ for controls
 *   CONSTAT     86DOS.asm:2968-2972  — system call 11: console status
 *   RAWIO       86DOS.asm:2988-2999  — system call 6:  raw I/O
 *   RAWINP      86DOS.asm:2994-2996  — system call 7:  raw input
 *   IN          86DOS.asm:2983-2986  — internal: read char (checking Ctrl-S/P/C)
 *   INCHK       86DOS.asm:2871-2934  — check for pending input / handle Ctrl chars
 *   LIST        86DOS.asm:3001-3004  — system call 5:  list/printer output
 *   PRTBUF      86DOS.asm:3006-3013  — system call 9:  print $-terminated string
 *   OUTMES      86DOS.asm:3015-3021  — internal: print CS-relative $-terminated string
 *   CRLF        86DOS.asm:2652-2656  — emit CR+LF
 *   TAB         86DOS.asm:2946-2955  — expand tab to next tab stop
 *   CTRLOUT     86DOS.asm:2935-2965  — handle control characters in OUT
 *   BUFIN       86DOS.asm:2566-2651  — system call 10: buffered line input
 *   BACKUP      86DOS.asm:2726-2735  — erase last character on screen
 *   BACKMES     86DOS.asm:2729-2735  — send BS-SP-BS
 *   KILNEW      86DOS.asm:2662-2671  — Ctrl-X: cancel current line
 *   ESC handler 86DOS.asm:2623-2634  — process ESC + command char
 *   COPYLIN     86DOS.asm:2741-2748  — copy rest of template
 *   COPYONE     86DOS.asm:2750-2762  — copy one char from template
 *   COPYSTR     86DOS.asm:2746-2748  — copy up to search char
 *   SKIPONE     86DOS.asm:2766-2771  — skip one template char
 *   SKIPSTR     86DOS.asm:2773-2777  — skip to search char
 *   FINDOLD     86DOS.asm:2779-2803  — find char in template
 *   REEDIT      86DOS.asm:2805-2817  — re-edit (ESC @)
 *   ENTERINS    86DOS.asm:2819-2821  — enter insert mode
 *   EXITINS     86DOS.asm:2823-2825  — exit insert mode
 */

#include <string.h>
#include "../include/dos.h"

/* -----------------------------------------------------------------------
 * con_out — Output one character to the console, tracking column position
 *           and handling Ctrl chars.  Also handles Ctrl-S/P/C if a key
 *           is pending (INCHK path).
 *
 * ASM: OUT / CONOUT  86DOS.asm:2853-2934
 * ----------------------------------------------------------------------- */
void con_out(byte ch)
{
    /* CTRLOUT: handle control characters */
    if (ch < 0x20) {
        switch (ch) {
        case '\r':   /* CR: zero column position */
            dos->CARPOS = 0;
            BIOSOUT(ch);
            if (dos->PFLAG) BIOSPRINT(ch);
            return;
        case '\b':   /* BS: decrement column position */
            dos->CARPOS--;
            BIOSOUT(ch);
            if (dos->PFLAG) BIOSPRINT(ch);
            return;
        case '\t': {  /* TAB: expand to next 8-column stop */
            byte spaces = (byte)(8 - (dos->CARPOS & 7));
            byte i;
            for (i = 0; i < spaces; i++)
                con_out(' ');
            return;
        }
        default:
            BIOSOUT(ch);
            if (dos->PFLAG) BIOSPRINT(ch);
            return;
        }
    }

    /* Normal printable character (or DEL = 0x7F) */
    if (ch != 0x7F)
        dos->CARPOS++;

    BIOSOUT(ch);
    if (dos->PFLAG) BIOSPRINT(ch);

    /* STATCHK: if a key is pending, read and process it */
    if (BIOSSTAT()) {
        /* INCHK path */
        byte k = BIOSIN();
        if (k == ('S' - '@')) {          /* Ctrl-S: pause */
            BIOSIN();                    /* wait for any key */
        }
        if (k == ('P' - '@')) {          /* Ctrl-P: printer echo on */
            dos->PFLAG = 1;
        } else if (k == ('N' - '@')) {   /* Ctrl-N: printer echo off */
            dos->PFLAG = 0;
        } else if (k == ('C' - '@')) {   /* Ctrl-C: abort */
            /* In the real kernel this jumps to RESTREG / Ctrl-C handler.
             * In C we can only signal via a flag or longjmp.  We print
             * the cancel symbol and note it.                            */
            if (dos->FUNC == 10 || dos->FUNC == 9) {
                con_out('\\');
                con_crlf();
            }
            /* NOTE: cannot fully replicate the stack manipulation that
             * restores user registers and calls INT CONTC.  A full port
             * would need setjmp/longjmp or a separate abort mechanism.  */
        }
    }
}

/* -----------------------------------------------------------------------
 * con_crlf — Emit CR then LF.
 *
 * ASM: CRLF  86DOS.asm:2652-2656
 * ----------------------------------------------------------------------- */
void con_crlf(void)
{
    con_out('\r');
    con_out('\n');
}

/* -----------------------------------------------------------------------
 * con_outmes — Print a '$'-terminated message string.
 *
 * ASM: OUTMES  86DOS.asm:3015-3021
 *
 * Used for internal kernel messages (like error strings).
 * ----------------------------------------------------------------------- */
void con_outmes(const byte *msg)
{
    while (*msg != '$')
        con_out(*msg++);
}

/* -----------------------------------------------------------------------
 * bufout — Output one character with '^' prefix for control chars.
 *
 * ASM: BUFOUT  86DOS.asm:2841-2851
 * ----------------------------------------------------------------------- */
static void bufout(byte ch)
{
    if (ch >= ' ' || ch == '\t') {
        con_out(ch);
        return;
    }
    con_out('^');
    con_out((byte)(ch | 0x40));
}

/* -----------------------------------------------------------------------
 * backup — Erase last character on the screen (BS SP BS).
 *
 * ASM: BACKMES  86DOS.asm:2729-2735  /  BACKUP  86DOS.asm:2726-2728
 * ----------------------------------------------------------------------- */
static void backmes(void)
{
    con_out('\b');
    con_out(' ');
    con_out('\b');
}

static void backup(byte *di_inout)
{
    (*di_inout)--;   /* DEC DH: one fewer char in new buffer */
    backmes();
}

/* -----------------------------------------------------------------------
 * fn_bufin — Buffered line input (system call 10).
 *
 * ASM: BUFIN  86DOS.asm:2566-2651
 *
 * Inputs:
 *   buf — pointer to input buffer descriptor:
 *           buf[0] = maximum chars (including CR)
 *           buf[1] = number of chars in template (set by prev call) or 0
 *           buf[2..] = template + new input area
 *
 * The buffer has a "template" from the previous call and allows editing
 * with ESC sequences (F1-F6 equivalents):
 *   ESC ESC  → literal ESC char
 *   ESC F    → exit insert mode
 *   ESC V    → enter insert mode  (ENTERINS)
 *   ESC H    → backspace
 *   ESC @    → re-edit (REEDIT)
 *   ESC \    → kill new line (KILNEW)
 *   ESC E    → copy rest of line (COPYLIN)
 *   ESC X    → skip to char (SKIPSTR)
 *   ESC ]    → copy to char (COPYSTR)
 *   ESC K    → skip one (SKIPONE)
 *   ESC M    → copy one (COPYONE)
 *
 * ASM uses a template pointer in SI / BH to walk the old template.
 * We mirror that with explicit pointers into dos->INBUF.
 *
 * NOTE: This is a faithful C port of the line-editing state machine.
 *       The ESC function table (ESCFUNC, line 2827) dispatches on pairs
 *       of ESC + command character.  We replicate it as a switch.
 * ----------------------------------------------------------------------- */

/* ESC command character table (ESCTAB) and function dispatch.
 * ASM: ESCTAB  (not shown explicitly but implied by ESCFUNC).
 * The ESC key is followed by one of these characters; the position in
 * the table determines which function is called.                        */
#define ESCTAB_LEN  12
static const byte esctab[ESCTAB_LEN] = {
    ESCCH,    /* 0: TWOESC  — literal ESC */
    'F',      /* 1: EXITINS */
    'V',      /* 2: ENTERINS */
    'H',      /* 3: BACKSP */
    '@',      /* 4: REEDIT */
    '\\',     /* 5: KILNEW */
    'E',      /* 6: COPYLIN */
    'X',      /* 7: SKIPSTR */
    ']',      /* 8: COPYSTR */
    'K',      /* 9: SKIPONE */
    'M',      /* 10: COPYONE */
    '\0'      /* sentinel */
};

void fn_bufin(byte *buf)
{
    byte max_chars = buf[0];   /* maximum input length (including CR) */
    byte tmpl_len  = buf[1];   /* length of template from previous call */

    if (max_chars == 0)
        return;

    byte *new_area = buf + 2;  /* where new chars are stored */

    /* Template: if previous call stored a CR-terminated line, use it */
    byte *old_buf  = dos->INBUF;  /* template in INBUF */
    byte  bl       = tmpl_len;    /* template length */
    byte  bh       = 0;           /* position in template (index) */

    /* Check for valid template (last char must be CR = 0x0D) */
    if (bl == 0 || bl > max_chars || old_buf[bl - 1] != '\r') {
        bl = 0;
        bh = 0;
    } else {
        /* valid template: check if template ends with CR */
        if (old_buf[bl] != '\r')
            bh = 0;  /* no edit: template invalid */
    }

    /* DL = max_chars - 1 (space for chars before CR) */
    byte dl    = (byte)(max_chars - 1);
    byte dh    = 0;           /* number of chars typed so far */
    byte insert_mode = 0;     /* AH: 0=overwrite, -1=insert */
    byte *di   = dos->INBUF;  /* pointer into new line buffer */
    byte *si   = old_buf;     /* template pointer */

    /* Save start column */
    dos->STARTPOS = dos->CARPOS;

getch:
    {
        byte ch = fn_in();

        /* Dispatch on character */
        if (ch == 0x7F || ch == '\b') {
            /* Backspace */
            if (dh != 0) {
                backup(&dh);
                di--;
                byte prev = *di;
                if (prev < ' ' && prev != '\t')
                    backmes();   /* extra BS-SP-BS for control chars */
            }
            if (!insert_mode) {
                if (bh != 0) { bh--; si--; }
            }
            goto getch;
        }

        if (ch == '\r') {
            /* End of line */
            *di++ = '\r';
            dh++;
            con_out('\r');
            /* Copy new buffer back */
            buf[1] = dh;
            memcpy(new_area, dos->INBUF, dh);
            /* Also update INBUF template */
            return;
        }

        if (ch == '\n') {
            /* Physical CR+LF */
            con_crlf();
            goto getch;
        }

        if (ch == ('X' - '@')) {
            /* Ctrl-X / KILNEW: cancel line */
            con_out('\\');
            /* Pop new-line: start over */
            di  = dos->INBUF;
            dh  = 0;
            si  = old_buf;
            bh  = 0;
            con_crlf();
            /* TAB to STARTPOS */
            {
                byte sp = dos->STARTPOS;
                byte i;
                for (i = dos->CARPOS; i < sp; i++)
                    con_out(' ');
            }
            goto getch;
        }

        if (ch == ESCCH) {
            /* ESC: read next command char */
            byte cmd = fn_in();
            int idx;
            for (idx = 0; idx < ESCTAB_LEN; idx++) {
                if (esctab[idx] == cmd) break;
            }
            if (idx >= ESCTAB_LEN) goto getch;  /* unknown → ignore */
            switch (idx) {
            case 0:  /* TWOESC: literal ESC */
                ch = ESCCH;
                goto savch;
            case 1:  /* EXITINS */
                insert_mode = 0;
                goto getch;
            case 2:  /* ENTERINS */
                insert_mode = 0xFF;
                goto getch;
            case 3:  /* BACKSP */
                ch = '\b';
                goto do_backsp_esc;
            case 4:  /* REEDIT: @-sign, re-display and restart with current as template */
                con_out('@');
                /* Copy current di into INBUF (already there), update template */
                {
                    byte old_dh = dh;
                    memcpy(dos->INBUF, new_area, old_dh);
                    bl = dh;
                    bh = dh;
                    si = old_buf + bh;
                    di = dos->INBUF;
                    dh = 0;
                }
                con_crlf();
                {
                    byte sp = dos->STARTPOS;
                    byte i;
                    for (i = dos->CARPOS; i < sp; i++) con_out(' ');
                }
                goto getch;
            case 5:  /* KILNEW: \ + new line */
                con_out('\\');
                di = dos->INBUF; dh = 0; si = old_buf; bh = 0;
                con_crlf();
                {
                    byte sp = dos->STARTPOS;
                    byte i;
                    for (i = dos->CARPOS; i < sp; i++) con_out(' ');
                }
                goto getch;
            case 6:  /* COPYLIN: copy rest of template */
                {
                    byte cnt = (byte)(bl - bh);
                    while (cnt > 0 && dh < dl) {
                        byte tc = old_buf[bh];
                        *di++ = tc;
                        bufout(tc);
                        bh++; dh++; cnt--;
                    }
                }
                goto getch;
            case 7:  /* SKIPSTR: skip to char in template */
                {
                    byte target = fn_in();
                    while (bh < bl && old_buf[bh] != target)
                        bh++;
                }
                goto getch;
            case 8:  /* COPYSTR: copy to char in template */
                {
                    byte target = fn_in();
                    while (bh < bl && old_buf[bh] != target && dh < dl) {
                        byte tc = old_buf[bh++];
                        *di++ = tc;
                        bufout(tc);
                        dh++;
                    }
                }
                goto getch;
            case 9:  /* SKIPONE: skip one template char */
                if (bh < bl) { bh++; si++; }
                goto getch;
            case 10: /* COPYONE: copy one template char */
                if (bh < bl && dh < dl) {
                    byte tc = old_buf[bh++];
                    *di++ = tc;
                    bufout(tc);
                    dh++;
                }
                goto getch;
            default:
                goto getch;
            }
        do_backsp_esc:
            if (dh != 0) {
                backup(&dh);
                di--;
            }
            if (!insert_mode && bh != 0) { bh--; si--; }
            goto getch;
        }

savch:
        /* Normal character: store and echo */
        if (dh >= dl) goto getch;   /* buffer full */
        *di++ = ch;
        dh++;
        bufout(ch);

        /* Advance template pointer if in overwrite mode */
        if (!insert_mode) {
            if (bh < bl) { bh++; si++; }
        }
        goto getch;
    }
}

/* -----------------------------------------------------------------------
 * fn_in — Internal console input: read char, handling Ctrl-S/P/C.
 *
 * ASM: IN  86DOS.asm:2983-2986
 * ----------------------------------------------------------------------- */
byte fn_in(void)
{
    byte ch;
again:
    /* INCHK: check for pending char (always check first via BIOSIN) */
    ch = BIOSIN();
    if (ch == ('S' - '@')) {       /* Ctrl-S: pause until key */
        BIOSIN();
        goto again;
    }
    if (ch == ('P' - '@')) {
        dos->PFLAG = 1;
        goto again;
    }
    if (ch == ('N' - '@')) {
        dos->PFLAG = 0;
        goto again;
    }
    if (ch == ('C' - '@')) {
        if (dos->FUNC == 10 || dos->FUNC == 9) {
            con_out('\\');
            con_crlf();
        }
        /* NOTE: full Ctrl-C handling requires stack restore + INT CONTC.
         * We re-read after printing the cancel indicator.               */
        goto again;
    }
    return ch;
}

/* -----------------------------------------------------------------------
 * fn_conin — Console input with echo (system call 1).
 *
 * ASM: CONIN  86DOS.asm:2975-2980
 * ----------------------------------------------------------------------- */
byte fn_conin(void)
{
    byte ch = fn_in();
    con_out(ch);
    return ch;
}

/* -----------------------------------------------------------------------
 * fn_conout — Console output (system call 2).
 *
 * ASM: CONOUT  86DOS.asm:2853-2854
 * ----------------------------------------------------------------------- */
void fn_conout(byte ch)
{
    con_out(ch);
}

/* -----------------------------------------------------------------------
 * fn_constat — Console status (system call 11).
 *
 * ASM: CONSTAT  86DOS.asm:2968-2972
 *
 * Returns 0xFF if a character is ready, 0 otherwise.
 * ----------------------------------------------------------------------- */
byte fn_constat(void)
{
    if (BIOSSTAT())
        return 0xFF;
    return 0;
}

/* -----------------------------------------------------------------------
 * fn_rawio — Raw I/O (system call 6).
 *
 * ASM: RAWIO  86DOS.asm:2988-2999
 *
 * DL = 0xFF → read status; if char available return it, else return 0.
 * DL != 0xFF → write DL to console without echo.
 * ----------------------------------------------------------------------- */
byte fn_rawio(byte dl)
{
    if (dl == 0xFF) {
        /* Input: check status first */
        if (!BIOSSTAT())
            return 0;
        return BIOSIN();   /* RAWINP */
    }
    /* Output */
    BIOSOUT(dl);
    return 0;
}

/* -----------------------------------------------------------------------
 * fn_rawinp — Raw input (system call 7): read without echo or Ctrl check.
 *
 * ASM: RAWINP  86DOS.asm:2994-2996
 * ----------------------------------------------------------------------- */
byte fn_rawinp(void)
{
    return BIOSIN();
}

/* -----------------------------------------------------------------------
 * fn_list — Printer output (system call 5).
 *
 * ASM: LIST  86DOS.asm:3001-3004
 * ----------------------------------------------------------------------- */
void fn_list(byte ch)
{
    BIOSPRINT(ch);
}

/* -----------------------------------------------------------------------
 * fn_prtbuf — Print '$'-terminated string (system call 9).
 *
 * ASM: PRTBUF  86DOS.asm:3006-3013
 * ----------------------------------------------------------------------- */
void fn_prtbuf(byte *str)
{
    while (*str != '$')
        con_out(*str++);
}
