# AGENTS.md — Rules for AI Agents Working on This Project

This file governs the behaviour of all AI agents (automated or interactive) contributing
to the 86-DOS → C translation project.

---

## 1. Scope and directory boundaries

- **All work must stay inside this directory** (`msdos-1.0/`) unless the user explicitly
  grants permission for a specific action outside it.
- Do **not** install system packages, modify shell configuration, or write files to any
  path outside this directory without asking first.
- Build artefacts (object files, executables, etc.) must also be placed inside this
  directory (e.g. under `build/`).

## 2. Source of truth

- `86DOS.asm` is the authoritative reference. It must **never be modified**.
- Every design decision in the C translation must be traceable back to a specific section
  of `86DOS.asm`. When in doubt, re-read the ASM.

## 3. Commit hygiene

- Always include `opencode_interaction.jsonl` in every commit (it is a project log and
  must stay current).
- Commit messages must be concise and describe *what* changed and *why*.
- Do not commit broken code; the project must build (or be clearly documented as
  work-in-progress) at every commit.

## 4. File and naming conventions

- C source files live under `src/`.  Header files live under `include/`.
- One logical subsystem per file, mirroring the sections of `86DOS.asm`
  (e.g. `src/fat.c`, `src/console.c`, `src/syscall.c`).
- Public symbols are prefixed with the subsystem name (e.g. `fat_unpack()`,
  `con_input()`).
- Constants and macros that directly correspond to ASM equates must carry the same name
  in upper-case (e.g. `MAXCALL`, `INTBASE`, `BIOSSEG`).

## 5. Fidelity vs. portability

- **Fidelity first**: the primary goal is a C translation that mirrors the ASM logic as
  closely as possible, not a clean modern rewrite.
- Use `uint8_t`, `uint16_t`, `uint32_t` from `<stdint.h>` everywhere — the original code
  is strictly 16-bit.
- Segment arithmetic that cannot be expressed in portable C may use helper macros or
  comments that explain the original segment:offset model.
- **Do not** silently simplify or "improve" behaviour; instead add a comment
  `/* NOTE: differs from ASM because … */` when a deviation is unavoidable.

## 6. Documentation

- Every C function that corresponds to an ASM routine must begin with a comment block
  that includes:
  - The ASM label(s) it replaces.
  - The line range in `86DOS.asm` (e.g. `/* ASM: UNPACK  86DOS.asm:369-395 */`).
  - A description of inputs, outputs, and side-effects matching the ASM comments.
- Do not delete or paraphrase the original ASM comments — quote them verbatim when
  useful.

## 7. Testing

- Where possible, write small unit tests under `tests/` that verify the C logic against
  known good inputs derived from the ASM specification.
- Tests must not depend on external services, network, or hardware.

## 8. Asking permission

If any of the following situations arise, **stop and ask the user** before proceeding:

- Writing anything outside this directory.
- Deleting or overwriting any existing file in this directory (other than files created
  by the translation project itself).
- Making a design choice that has multiple equally valid interpretations in the ASM.
- Adding a dependency on a library not already present on the system.

## 9. What agents must NOT do

- Must not modify `86DOS.asm`.
- Must not commit secrets, credentials, or binary blobs.
- Must not run interactive or long-running processes without the user's knowledge.
- Must not make changes that break a previously working build without explaining why.
