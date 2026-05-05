# msdos-1.0

> **Source of the original ASM code:**
> The file `86DOS.asm` was obtained from
> [DOS-History/Paterson-Listings](https://github.com/DOS-History/Paterson-Listings/blob/main/3_source_code/86-DOS_1.00/86DOS.ASM)
> and is reproduced here unchanged as the read-only reference for all translation work.

## Purpose

This repository explores how AI agents can be used to better understand the
original MS-DOS 1.0 (86-DOS) source code.

The goal is faithful translations of `86DOS.asm` — not modernised rewrites, but
line-by-line renderings that make the original logic readable and testable in a
contemporary environment.  Every design decision is traceable back to a specific
label and line range in `86DOS.asm`.  Three independent translations are provided:
one in **C**, one in **Rust**, and one in **Ada**.

Everything in this repository — the translations, the test suites, the
documentation, and the commit history — was produced through interactive sessions
with **[OpenCode](https://opencode.ai)**, an AI coding agent.  The complete,
unedited log of those interactions is preserved in
[`opencode_interaction.jsonl`](opencode_interaction.jsonl).

## Repository layout

```
86DOS.asm                    # READ-ONLY original source (3 622 lines)
opencode_interaction.jsonl   # full OpenCode session log
AGENTS.md                    # rules governing AI agent behaviour in this repo
LICENSE                      # MIT licence
README_C.md                  # detailed notes on the C translation
README_Rust.md               # detailed notes on the Rust translation
README_Ada.md                # detailed notes on the Ada translation
Makefile                     # builds and tests the C translation
c/
  include/                   # public headers (dos_types.h, fcb.h, dpb.h, …)
  src/                       # C translation, one file per subsystem
    fat.c                    # FAT read/write, cluster allocation
    disk.c                   # low-level sector and directory I/O
    directory.c              # directory search and name parsing
    file.c                   # open, close, create, delete, rename
    io.c                     # sequential and random record I/O
    console.c                # console input/output, buffered line editor
    fcb_util.c               # FCB utilities, search-first/next
    syscall.c                # system-call dispatcher
    init.c                   # DOS kernel initialisation
  tests/                     # C unit tests
rust/
  Cargo.toml
  src/                       # Rust translation, one module per subsystem
    lib.rs                   # crate root; DosState struct
    types.rs                 # FCB, DPB, BiosVtable trait, DosError
    fat.rs                   # FAT read/write, cluster allocation
    disk.rs                  # low-level sector and directory I/O
    directory.rs             # directory search and name parsing
    file.rs                  # open, close, create, delete, rename
    io.rs                    # sequential and random record I/O
    console.rs               # console input/output, buffered line editor
    fcb_util.rs              # FCB utilities, search-first/next
    syscall.rs               # system-call dispatcher
    init.rs                  # DOS kernel initialisation
  tests/                     # Rust unit tests
ada/
  Makefile
  src/                       # Ada translation, one package per subsystem
    dos86.ads / .adb         # root package: types, constants, DPB, FCB, Dos_State
    dos86-fat.ads / .adb     # FAT read/write, cluster allocation
    dos86-disk.ads / .adb    # low-level sector and directory I/O
    dos86-directory.ads / .adb  # directory search and name parsing
    dos86-file_ops.ads / .adb   # open, close, create, delete, rename
    dos86-io_ops.ads / .adb     # sequential and random record I/O
    dos86-console.ads / .adb    # console input/output, buffered line editor
    dos86-fcb_util.ads / .adb   # FCB utilities, search-first/next
    dos86-syscall.ads / .adb    # system-call dispatcher
    dos86-init.ads / .adb       # DOS kernel initialisation
  tests/                     # Ada unit tests
```

## Building and testing

### C translation

```sh
make        # build everything (requires gcc or clang, C99)
make check  # build and run all unit tests
```

### Rust translation

```sh
cd rust
cargo build   # compile the library crate (requires Rust stable ≥ 1.70)
cargo test    # compile and run all unit tests
```

### Ada translation

```sh
cd ada
make          # compile all library sources (requires GNAT 14 / gnatmake)
make check    # build and run all unit tests
```

## Licence

The translations and associated files are released under the
[MIT Licence](LICENSE).  The original `86DOS.asm` is a historical artefact
reproduced for reference only; its copyright belongs to its respective owners.
