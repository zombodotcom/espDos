#!/usr/bin/env python3
"""
scp_to_nasm.py — Translate SCP-ASM dialect of 86DOS.ASM to NASM.

86DOS.ASM was written for Seattle Computer Products' ASM 2.43 assembler.
NASM doesn't speak that dialect natively. Rather than edit Tim Paterson's
source, we apply mechanical translations here, producing a NASM-ready
.asm on stdout. The original source is never modified.

Each rule below is documented inline. The goal is byte-identical output
to what SCP-ASM 2.43 would produce — only the surface syntax changes.

Rules:

  R1  String-op shortcuts (4-letter mnemonics SCP-ASM accepted as
      operandless versions of the corresponding 8086 string ops):
        LODB→lodsb  LODW→lodsw  STOB→stosb  STOW→stosw
        MOVB→movsb  MOVW→movsw  CMPB→cmpsb  CMPW→cmpsw
        SCAB→scasb  SCAW→scasw

  R2  Direction-flag shortcuts:
        DOWN→std  UP→cld

  R3  Standalone segment override: `SEG <reg>` on its own line is a
      one-instruction prefix for the next line's memory operand. We
      fold `<reg>:` into the next line's first '[' (or emit the prefix
      byte directly if there is no [memory] operand).

  R4  Unary shifts/rotates default to count 1:
        SHL R → shl R, 1   (also SHR/SAR/SAL/ROL/ROR/RCL/RCR)

  R5  Two-operand DIV/MUL/IMUL — SCP names the implicit destination
      (DIV AX,SI). NASM omits it: div si.

  R6  `DS n` (Define Storage) → `resb n` inside absolute (struct)
      sections; `times n db 0` inside code sections.

  R7  Multiple `ORG 0` declarations at the top of the file are field
      layouts (FCB / DPB / BIOSSEG / register-save-frame). Translate
      to NASM `absolute 0` blocks. The first `ORG 0` followed by
      `PUT NNN` switches us into emitting code at origin NNN.

  R8  `IF <expr>` / `ENDIF` → `%if <expr>` / `%endif` (conditional
      assembly).

  R9  `JP <label>` → `jmp <label>` (SCP shorthand for unconditional
      jump; NASM has JP as a synonym for `JPE`/jump-if-parity, which
      is not what 86DOS.ASM means by JP).

  R10 `ALIGN` (no arg) → `align 2`.

  R11 Rename labels that collide with NASM keywords:
        DEFAULT → SCPDEFAULT

Usage:
    python scp_to_nasm.py 86DOS.ASM > kernel.nasm
or  python scp_to_nasm.py < 86DOS.ASM > kernel.nasm
"""

import re
import sys


# ----- Rule tables ---------------------------------------------------

_SHORTCUTS = {
    'LODB': 'lodsb', 'LODW': 'lodsw',
    'STOB': 'stosb', 'STOW': 'stosw',
    'MOVB': 'movsb', 'MOVW': 'movsw',
    'CMPB': 'cmpsb', 'CMPW': 'cmpsw',
    'SCAB': 'scasb', 'SCAW': 'scasw',
    'DOWN': 'std',   'UP':   'cld',
    'DI':   'cli',   'EI':   'sti',
}

_RENAME = {
    'DEFAULT': 'SCPDEFAULT',
}

_SHIFT_OPS = ('SHL', 'SHR', 'SAR', 'SAL', 'ROL', 'ROR', 'RCL', 'RCR')

_SEG_PREFIX_BYTES = {'ES': 0x26, 'CS': 0x2E, 'SS': 0x36, 'DS': 0x3E}


# ----- Regexes -------------------------------------------------------

_RE_SHORTCUT = re.compile(
    r'^(?P<lead>\s*)(?P<word>[A-Za-z]{2,4})\s*(?P<comment>;.*)?$'
)

_RE_SEG = re.compile(
    r'^\s*SEG\s+(?P<reg>ES|CS|SS|DS)\s*(?:;.*)?$',
    re.IGNORECASE,
)

_RE_UNARY_SHIFT = re.compile(
    r'^(?P<lead>\s*)(?P<op>' + '|'.join(_SHIFT_OPS) + r')\s+'
    r'(?P<reg>[A-Za-z][A-Za-z0-9_+\[\]:]*)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

_RE_DIVMUL = re.compile(
    r'^(?P<lead>\s*)(?P<op>DIV|MUL|IMUL)\s+'
    r'(?P<dst>AX|AL|DX|DL),\s*(?P<rhs>.+?)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

_RE_DS = re.compile(
    r'^(?P<label>[A-Za-z_][A-Za-z0-9_]*:?)?\s*DS\s+(?P<count>\S+)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

_RE_ORG = re.compile(
    r'^\s*ORG\s+(?P<expr>.+?)\s*(?:;.*)?$',
    re.IGNORECASE,
)

_RE_PUT = re.compile(
    r'^\s*PUT\s+(?P<expr>.+?)\s*(?:;.*)?$',
    re.IGNORECASE,
)

_RE_IF = re.compile(
    r'^\s*IF\s+(?P<expr>.+?)\s*(?:;.*)?$',
    re.IGNORECASE,
)

_RE_ENDIF = re.compile(
    r'^\s*ENDIF\s*(?:;.*)?$',
    re.IGNORECASE,
)

_RE_JP = re.compile(
    r'^(?P<lead>\s*)JP\s+(?P<target>\S+)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R12: SCP `SBC dst, src` is NASM `sbb dst, src`.
_RE_SBC = re.compile(
    r'^(?P<lead>\s*)SBC\s+(?P<rest>.+?)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R13: `J<cond> RET` is SCP for "conditionally return now." NASM has
# no equivalent; we expand to inverted-jump + inline `ret`.
_CONDITIONALS = (
    'JZ', 'JNZ', 'JC', 'JNC', 'JS', 'JNS', 'JO', 'JNO', 'JP', 'JNP',
    'JE', 'JNE', 'JA', 'JNA', 'JB', 'JNB', 'JG', 'JNG', 'JL', 'JNL',
    'JAE', 'JBE', 'JGE', 'JLE', 'JNAE', 'JNBE', 'JNGE', 'JNLE', 'JPE', 'JPO',
)
_RE_JCC_RET = re.compile(
    r'^(?P<lead>\s*)(?P<jcc>' + '|'.join(_CONDITIONALS) + r')\s+RET\b\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R13b: `Jcc <label>` → `Jnotcc .skip; JMP <label>; .skip:`
#
# The 8086 only supports `Jcc rel8` (1-byte signed offset, ±127 bytes).
# NASM at default CPU level (>= 386) silently upgrades any out-of-range
# `Jcc` to the 80386+ `0F 8x rel16` form. 8086tiny doesn't decode the
# `0F 8x` two-byte opcodes — it consumes `0F` as `POP CS` and falls into
# garbage on the rest, which corrupts memory. The kernel's `JB CTRLOUT`
# at file offset 0x102D (target 131 bytes away — just past rel8 range)
# was the smoking gun: 4 bytes `0F 82 81 00` got mis-decoded as
# `ADD WORD [BX+SI], 0x7F3C`, scribbling random data into DATMES.
#
# Fix: expand every `Jcc target` to a 5-byte sequence
#   Jnotcc .skip
#   JMP    target
#   .skip:
# This always uses 8086-legal opcodes (Jcc rel8 + JMP rel16) and works
# regardless of distance. Minor cost: kernel grows by ~3 bytes per
# conditional jump expanded.
_RE_JCC_LABEL = re.compile(
    r'^(?P<lead>\s*)(?P<jcc>' + '|'.join(_CONDITIONALS) + r')\s+'
    r'(?P<target>[A-Za-z_]\w*)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

def _invert_jcc(jcc):
    """Toggle the N: JZ↔JNZ, JC↔JNC, JBE↔JNBE, etc."""
    j = jcc.upper()
    if j.startswith('JN'):
        return 'J' + j[2:]
    return 'JN' + j[1:]


# R14: SCP `<op> B, ...` and `<op> W, ...` are byte/word size hints
# preceding a memory operand. NASM uses `byte` / `word` inline.
_RE_SIZE_HINT = re.compile(
    r'^(?P<lead>\s*)(?P<op>[A-Za-z]{2,5})\s+(?P<size>B|W),\s*(?P<rest>.+?)\s*(?P<comment>;.*)?$',
)

# R15: bare `PUSH [mem]` / `POP [mem]` need explicit `word` in NASM.
_RE_PUSH_POP_MEM = re.compile(
    r'^(?P<lead>\s*)(?P<op>PUSH|POP)\s+(?P<rest>\[[^]]+\])\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R16: SCP `CALL <offset>, <segment>` and `JMP <offset>, <segment>`
# are far-call/jmp with offset first, then segment. NASM: `call far
# <segment>:<offset>`.
_RE_FAR_CALLJMP = re.compile(
    r'^(?P<lead>\s*)(?P<op>CALL|JMP)\s+(?P<offset>[A-Za-z_][\w]*)\s*,\s*'
    r'(?P<seg>[A-Za-z_][\w]*)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R17: SCP `JMP L, [mem]` is far indirect jump (`L` = long).
_RE_JMP_L_INDIRECT = re.compile(
    r'^(?P<lead>\s*)JMP\s+L\s*,\s*(?P<rest>\[[^]]+\])\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R18: SCP `MOV [mem], imm` with no explicit size hint. SCP defaulted
# to word; NASM requires the size. We add `word` only when the source
# is a literal/identifier (not a register, which already disambiguates).
_RE_MOV_MEM_IMM = re.compile(
    r'^(?P<lead>\s*)MOV\s+(?P<mem>\[[^]]+\])\s*,\s*(?P<src>[^;]+?)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R19: JCXZ has no inverse mnemonic; expand `JCXZ RET` specially.
_RE_JCXZ_RET = re.compile(
    r'^(?P<lead>\s*)JCXZ\s+RET\b\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R19b: `JCXZ <label>` — same problem as the other conditional jumps,
# but JCXZ has no inverse mnemonic so we can't reuse R13b's pattern.
# Expand to `JCXZ .take; JMP .skip; .take: JMP target; .skip:`.
_RE_JCXZ_LABEL = re.compile(
    r'^(?P<lead>\s*)JCXZ\s+(?P<target>[A-Za-z_]\w*)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R20: SCP `RET L` → NASM `retf` (far return).
_RE_RET_L = re.compile(
    r'^(?P<lead>\s*)RET\s+L\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R21: `INC [mem]` / `DEC [mem]` (no size) → add `word`.
_RE_INCDEC_MEM = re.compile(
    r'^(?P<lead>\s*)(?P<op>INC|DEC)\s+(?P<rest>\[[^]]+\])\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# R22: `CMP [mem], imm` (no register operand) → add `word`.
_RE_CMP_MEM_IMM = re.compile(
    r'^(?P<lead>\s*)CMP\s+(?P<mem>\[[^]]+\])\s*,\s*(?P<src>[^;]+?)\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)

# Registers that disambiguate operand size on their own.
_BYTE_REGS = {'AL', 'BL', 'CL', 'DL', 'AH', 'BH', 'CH', 'DH'}
_WORD_REGS = {'AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP', 'SP',
              'CS', 'DS', 'ES', 'SS'}

_RE_ALIGN = re.compile(
    r'^(?P<lead>\s*)ALIGN\s*(?P<comment>;.*)?$',
    re.IGNORECASE,
)


# ----- Translator ----------------------------------------------------

def _fold_seg_into_memop(reg, next_line):
    """If next_line contains '[mem]', inject `<reg>:` into the first
    bracket and return the modified line. Otherwise return None — the
    caller will emit a raw prefix byte and process next_line normally."""
    if '[' not in next_line:
        return None
    return next_line.replace('[', f"[{reg.lower()}:", 1)


def _seg_prefix_db(reg):
    prefix = _SEG_PREFIX_BYTES[reg.upper()]
    return f"\tdb 0x{prefix:02x}\t; SCP: SEG {reg.upper()} (no [mem] follows)\n"


def _rename_identifiers(line):
    """Apply R11 — rename SCP-source identifiers that collide with
    NASM keywords. We use word-boundary regex so we don't touch
    substrings."""
    for old, new in _RENAME.items():
        line = re.sub(r'\b' + re.escape(old) + r'\b', new, line)
    return line


def translate(lines):
    out = []
    pending_seg = None
    in_code = False  # flips True after the first PUT directive
    jcc_ret_counter = 0

    for raw in lines:
        line = raw.rstrip('\n')

        # Apply identifier renames first so subsequent matchers see
        # the post-rename text.
        line = _rename_identifiers(line)

        # R3a: pending SEG override from the prior line. Try to fold
        # into a memory operand on this line; if the line has no
        # memory operand, emit a raw segment-prefix byte and continue
        # processing the line through all other rules.
        if pending_seg is not None:
            folded = _fold_seg_into_memop(pending_seg, line)
            if folded is not None:
                # Folded successfully. Continue running rules on the
                # folded line so other transformations still apply
                # (e.g. MUL byte sizing).
                line = folded.rstrip('\n')
                pending_seg = None
            else:
                # No memory operand to fold into. Emit the prefix byte
                # and let the line itself flow through normal rules.
                out.append(_seg_prefix_db(pending_seg))
                pending_seg = None

        # R3b: detect SEG <reg> standalone.
        m = _RE_SEG.match(line)
        if m:
            pending_seg = m.group('reg')
            out.append(f"\t; SCP: SEG {pending_seg.upper()} folded into next operand\n")
            continue

        # R8: IF / ENDIF → %if / %endif.
        m = _RE_IF.match(line)
        if m:
            out.append(f"%if {m.group('expr')}\n")
            continue
        if _RE_ENDIF.match(line):
            out.append("%endif\n")
            continue

        # R7: ORG handling — depends on whether we've hit PUT yet.
        m = _RE_ORG.match(line)
        if m:
            expr = m.group('expr').strip()
            if not in_code:
                # Pre-PUT ORG: a struct-section reset.
                out.append(f"absolute {expr}\n")
            else:
                # Post-PUT ORG <expr> with a label as expr is the SCP
                # "init code overlaps data area" trick. NASM bin-mode
                # doesn't support multiple ORGs and can't accept
                # forward-referenced critical expressions. Our emulator
                # handles loading explicitly, so we don't need the
                # overlap. Emit linearly and note the original intent.
                out.append(f"\t; SCP: ORG {expr} (overlap-init trick, ignored — see preprocessor R7)\n")
            continue

        # PUT: switch to code mode and emit org.
        m = _RE_PUT.match(line)
        if m:
            expr = m.group('expr').strip()
            if not in_code:
                # First PUT after the leading struct ORGs — switch to a
                # code section and set origin.
                out.append("section .text\n")
                out.append(f"\torg {expr}\n")
                in_code = True
            else:
                # Later PUTs only adjust the load address relative to
                # current location (e.g. `PUT $+100H`). NASM flat
                # binary doesn't support multiple orgs; we treat these
                # as comments and trust subsequent label arithmetic to
                # work out. The kernel's INITCODE block uses this to
                # tell the assembler "init code is loaded 0x100 above
                # its assembly offset," which we don't need at the
                # assembler level since the kernel itself does the
                # relocation at boot.
                out.append(f"\t; SCP: PUT {expr} (load-address hint, ignored)\n")
            continue

        # R5: two-operand DIV/MUL/IMUL. SCP names the implicit
        # destination (AL→byte form, AX→word form). We strip the dst
        # and add a size hint to the memory operand if needed.
        m = _RE_DIVMUL.match(line)
        if m:
            op = m.group('op').lower()
            dst = m.group('dst').upper()
            rhs = m.group('rhs')
            comment = m.group('comment') or ''
            size = 'byte' if dst in ('AL', 'DL') else 'word'
            # Only add size if the operand starts with '[' (memory).
            # Register operands like `MUL AL, BL` already disambiguate.
            if rhs.lstrip().startswith('['):
                rhs_out = f"{size} {rhs}"
            else:
                rhs_out = rhs
            out.append(f"{m.group('lead')}{op} {rhs_out}{('  ' + comment) if comment else ''}\n")
            continue

        # R4: unary shift/rotate.
        m = _RE_UNARY_SHIFT.match(line)
        if m and ',' not in line.split(';', 1)[0]:
            op = m.group('op').lower()
            reg = m.group('reg')
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}{op} {reg}, 1{('  ' + comment) if comment else ''}\n")
            continue

        # R9 (must run BEFORE R13/R13b): SCP `JP <target>` is the
        # SCP shorthand for *unconditional* jump (a quirk of the
        # SCP-ASM dialect). NASM's `JP` is the parity-conditional
        # jump (= `JPE`), which is NOT what 86DOS.ASM means. If
        # R13/R13b ran first they would treat `JP <label>` as a
        # parity-conditional and mis-translate it — that's how
        # MYD's digit loop and BUFIN's GETCH loop got broken.
        m = _RE_JP.match(line)
        if m:
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}jmp {m.group('target')}{('  ' + comment) if comment else ''}\n")
            continue

        # R13: `J<cond> RET` → invert + inline ret. Must run before
        # the generic JP rule (since some conditionals overlap).
        m = _RE_JCC_RET.match(line)
        if m:
            jcc = m.group('jcc')
            inv = _invert_jcc(jcc).lower()
            label = f".scp_skip_{jcc_ret_counter:03d}"
            jcc_ret_counter += 1
            comment = m.group('comment') or ''
            note = f"  ; SCP: {jcc.upper()} RET (conditional return)"
            out.append(f"{m.group('lead')}{inv} {label}{note}\n")
            out.append(f"\tret\n")
            out.append(f"{label}:{('  ' + comment) if comment else ''}\n")
            continue

        # R13b: `J<cond> <label>` → `Jnotcc .skip; JMP <label>; .skip:`
        # Always expand so the assembled binary uses only 8086-legal
        # `Jcc rel8` + `JMP rel16` opcodes — never the 80386+ `0F 8x
        # rel16` form. (See the long comment by _RE_JCC_LABEL above.)
        m = _RE_JCC_LABEL.match(line)
        if m:
            jcc = m.group('jcc')
            target = m.group('target')
            inv = _invert_jcc(jcc).lower()
            label = f".scp_skip_{jcc_ret_counter:03d}"
            jcc_ret_counter += 1
            comment = m.group('comment') or ''
            note = f"  ; SCP: {jcc.upper()} {target} (long-form for 8086)"
            out.append(f"{m.group('lead')}{inv} {label}{note}\n")
            out.append(f"\tjmp {target}\n")
            out.append(f"{label}:{('  ' + comment) if comment else ''}\n")
            continue

        # R9 was moved above R13/R13b to fix `JP <label>` being
        # mis-interpreted as parity-conditional.

        # R12: SBC <ops> → sbb <ops>. Must run before the generic
        # shortcut matcher (SBC is 3 letters, would otherwise be
        # interpreted as a standalone keyword).
        m = _RE_SBC.match(line)
        if m:
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}sbb {m.group('rest')}{('  ' + comment) if comment else ''}\n")
            continue

        # R14: `<op> B, ...` / `<op> W, ...` size hints. SCP placed
        # the size before the memory operand; NASM puts it inline.
        # We only recognize this when the next operand starts with
        # '['; otherwise the `B`/`W` was a register and not a hint.
        m = _RE_SIZE_HINT.match(line)
        if m and m.group('rest').lstrip().startswith('['):
            op = m.group('op').lower()
            size = 'byte' if m.group('size').upper() == 'B' else 'word'
            rest = m.group('rest')
            comment = m.group('comment') or ''
            # Inject size before the first '['.
            rest_sized = rest.replace('[', f'{size} [', 1)
            out.append(f"{m.group('lead')}{op} {rest_sized}{('  ' + comment) if comment else ''}\n")
            continue

        # R15: bare PUSH/POP [mem] needs `word` size in NASM.
        m = _RE_PUSH_POP_MEM.match(line)
        if m:
            op = m.group('op').lower()
            rest = m.group('rest')
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}{op} word {rest}{('  ' + comment) if comment else ''}\n")
            continue

        # R17: JMP L, [mem] — far indirect jump.
        m = _RE_JMP_L_INDIRECT.match(line)
        if m:
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}jmp far {m.group('rest')}{('  ' + comment) if comment else ''}\n")
            continue

        # R16: SCP `CALL/JMP <offset>, <segment>` → NASM `call/jmp
        # <segment>:<offset>`. NASM auto-detects the far form when the
        # operand uses colon syntax with two immediate values.
        m = _RE_FAR_CALLJMP.match(line)
        if m:
            op = m.group('op').lower()
            seg = m.group('seg')
            offset = m.group('offset')
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}{op} {seg}:{offset}{('  ' + comment) if comment else ''}\n")
            continue

        # R19: JCXZ RET — JCXZ has no inverse; emit a 2-step skip.
        m = _RE_JCXZ_RET.match(line)
        if m:
            label_done = f".scp_jcxz_done_{jcc_ret_counter:03d}"
            label_ret  = f".scp_jcxz_ret_{jcc_ret_counter:03d}"
            jcc_ret_counter += 1
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}jcxz {label_ret}\n")
            out.append(f"\tjmp {label_done}\n")
            out.append(f"{label_ret}:\n")
            out.append(f"\tret\n")
            out.append(f"{label_done}:{('  ' + comment) if comment else ''}\n")
            continue

        # R19b: `JCXZ <label>` — same expansion shape as R19, but
        # branching to <label> instead of inlining a return. Required
        # because 8086's JCXZ is rel8-only and some targets exceed
        # ±127 bytes after kernel growth from R13b.
        m = _RE_JCXZ_LABEL.match(line)
        if m:
            target = m.group('target')
            label_take = f".scp_jcxz_take_{jcc_ret_counter:03d}"
            label_skip = f".scp_jcxz_skip_{jcc_ret_counter:03d}"
            jcc_ret_counter += 1
            comment = m.group('comment') or ''
            note = f"  ; SCP: JCXZ {target} (long-form for 8086)"
            out.append(f"{m.group('lead')}jcxz {label_take}{note}\n")
            out.append(f"\tjmp {label_skip}\n")
            out.append(f"{label_take}:\n")
            out.append(f"\tjmp {target}\n")
            out.append(f"{label_skip}:{('  ' + comment) if comment else ''}\n")
            continue

        # R18: `MOV [mem], imm` with no size and a non-register source.
        # SCP defaulted to word. We only add the size hint if the source
        # is not a register (registers already disambiguate the size).
        m = _RE_MOV_MEM_IMM.match(line)
        if m:
            src = m.group('src').strip()
            src_upper = src.upper()
            # If the source is a register name, NASM can size from the
            # register; pass the line through unchanged.
            if src_upper not in _BYTE_REGS and src_upper not in _WORD_REGS:
                mem = m.group('mem')
                comment = m.group('comment') or ''
                out.append(f"{m.group('lead')}mov word {mem}, {src}{('  ' + comment) if comment else ''}\n")
                continue
            # Else fall through to default pass-through.

        # R20: SCP `RET L` → NASM `retf` (far return).
        m = _RE_RET_L.match(line)
        if m:
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}retf{('  ' + comment) if comment else ''}\n")
            continue

        # R21: INC/DEC of memory, no size → word.
        m = _RE_INCDEC_MEM.match(line)
        if m:
            op = m.group('op').lower()
            rest = m.group('rest')
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}{op} word {rest}{('  ' + comment) if comment else ''}\n")
            continue

        # R22: `CMP [mem], imm` with non-register source → add word.
        m = _RE_CMP_MEM_IMM.match(line)
        if m:
            src = m.group('src').strip()
            src_upper = src.upper()
            if src_upper not in _BYTE_REGS and src_upper not in _WORD_REGS:
                mem = m.group('mem')
                comment = m.group('comment') or ''
                out.append(f"{m.group('lead')}cmp word {mem}, {src}{('  ' + comment) if comment else ''}\n")
                continue
            # Else fall through.

        # R10: standalone ALIGN → align 2.
        m = _RE_ALIGN.match(line)
        if m:
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}align 2{('  ' + comment) if comment else ''}\n")
            continue

        # R6: DS n.
        m = _RE_DS.match(line)
        if m and m.group('count'):
            label = m.group('label') or ''
            count = m.group('count')
            comment = m.group('comment') or ''
            if label:
                if not label.endswith(':'):
                    label = label + ':'
                out.append(f"{label}\n")
            if in_code:
                out.append(f"\ttimes {count} db 0{('  ' + comment) if comment else ''}\n")
            else:
                out.append(f"\tresb {count}{('  ' + comment) if comment else ''}\n")
            continue

        # R1+R2: standalone shortcut keyword (must be on a line with
        # no other tokens — we already handled multi-token forms above).
        m = _RE_SHORTCUT.match(line)
        if m and m.group('word').upper() in _SHORTCUTS:
            new_op = _SHORTCUTS[m.group('word').upper()]
            comment = m.group('comment') or ''
            out.append(f"{m.group('lead')}{new_op}{('  ' + comment) if comment else ''}\n")
            continue

        # Default: pass through (with renames applied).
        out.append(line + '\n')

    return out


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'r', encoding='ascii', errors='replace') as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()
    sys.stdout.write(''.join(translate(lines)))


if __name__ == '__main__':
    main()
