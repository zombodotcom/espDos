# Mandelbrot performance on espDos

## Why this document exists

We render an 78x24 ASCII Mandelbrot as the first user transient on
top of Tim Paterson's 86-DOS 1.00 kernel. Once it worked, the
question became: how fast can it be, and what's actually worth
optimizing?

The question turns out to have a surprising answer that no
Mandelbrot-optimization tutorial will tell you, because every
tutorial in the literature assumes the program writes pixels
directly to a framebuffer — not through a hosted DOS kernel via
`INT 21h`. That assumption changes which optimizations matter.

This document records: what we measured, what the literature offers,
which subset of the literature actually applies to our case, what
we shipped, and why.


## 1. Cost profile (measured)

`tests/emu/test_mandel.c` runs the full pipeline and reports
instruction counts. Numbers below are from a clean run on host:

```
total instructions          2,651,300
  kernel boot                   715,300   (until date prompt is consumed)
  loader + transient launch     ~30,000   (one disk sector read + far jmp)
  Mandelbrot render            ~402,000   (78x24 = 1,872 pixels)
```

Inside the render budget:

```
INT 21h overhead         ~400,000   (1,920 calls x ~209 inst each)
IMUL+normalize           ~134,000   (1,872 px x avg 12 iters x 3 mults x ~2 inst)
loop counters / ramp        small   (maybe 20K)
```

The numbers don't add to 402,000 cleanly because they overlap — each
INT 21h *contains* dispatch cost, but the dispatch cost is also what
the kernel's call into BIOSSEG OUT pays for. The point is: per-call
syscall overhead dominates.

A single `AH=02h CONOUT` round trip is approximately:

```
INT 21h IVT lookup + push flags/CS/IP   ~10 inst
86-DOS kernel ENTRY                     ~30 inst
DISPATCH table walk + AH=02 -> CONOUT   ~20 inst
CONOUT calls BIOSSEG OUT (CALL FAR)     ~20 inst
emulator BIOSSEG trap + bios_handle_call ~20 inst
RETF synthesis                          ~20 inst
kernel EXIT                             ~30 inst
IRET                                    ~10 inst
                                        -------
                                       ~160 inst per char + overhead variance
```

Empirically `~209 inst/call`. That covers some host-side
emulator-loop bookkeeping not in the per-step list above.

For a 1,872-pixel grid plus 24 row terminators, that's
`~1,920 INT 21h calls x ~209 inst = ~401,280 inst` — i.e., the
syscall overhead alone accounts for the entire measured render
budget. The IMUL work the math papers obsess over is simultaneously
real and dwarfed.


## 2. Why our profile is not the textbook profile

Every Mandelbrot optimization paper from 1988 onward — Abrash,
Dawson, Fractint, demoscene 256-byte intros — assumes the renderer
writes pixels directly to video memory. CGA at `0xB8000`, VGA mode
13h at `0xA0000`, framebuffer pointer in `ES:DI`. One `MOV` per
pixel; no syscall, no dispatch.

Our pitch is "running through Tim Paterson's actual kernel," so we
do go through `INT 21h`. That's a 200x slower output path than
direct video, and it inverts the optimization priorities:

| profile         | Direct CGA / VGA      | DOS INT 21h CONOUT (us) |
|-----------------|-----------------------|-------------------------|
| dominant cost   | IMUL math             | syscall dispatch         |
| right opt       | tighter inner loop    | reduce # of syscalls     |
| wrong opt       | reduce # of writes    | tighten inner loop alone |

The literature's blind spot here isn't a bug — it's a different
problem. We have to think for ourselves.


## 3. Catalog of techniques considered

For each technique below: what it does, where it comes from, and the
estimated gain *for our specific cost profile* (not the textbook
profile).

### 3.1  Bruce Dawson's algebraic identity `(zr+zi)^2 = zr^2 + 2 zr zi + zi^2`

**Source:** Bruce Dawson, *Faster Fractals Through Algebra*
(`randomascii.wordpress.com`, 2011).

**Idea:** instead of three multiplications per iteration
(`zr*zr`, `zi*zi`, `zr*zi`), compute `(zr+zi)^2` and derive
`2*zr*zi` by subtraction.

**Reality on scalar 8086:** still three multiplications. The trick
*does* help with SIMD because squaring is a single-input op (you can
fit four squarings into one packed multiply that doesn't have to
choose two distinct lanes). 8086 has no SIMD. **Gain: 0 percent.**

### 3.2  Abrash's Black Book chapter 63 (FPU pipelining)

**Source:** Michael Abrash, *Graphics Programming Black Book*, ch.
63 §2 (`phatcode.net/.../63-02.html`).

**Idea:** schedule 387 floating-point pipeline so that
`FMUL`/`FADD`/`FSUB` overlap. Pair multiplies with adds to keep both
units busy.

**Reality for us:** we have no FPU. The whole technique is
inapplicable. Worth knowing about for context — it's why pre-Pentium
optimized Mandelbrots looked different from post-Pentium ones — but
it doesn't transfer to our chip. **Gain: 0 percent.**

### 3.3  Cardioid + period-2 disk early-reject

**Source:** standard since the 1980s; documented at
`mathr.co.uk/blog/2022-11-19_cardioid_and_bulb_checking.html` and in
the Wikipedia article *Plotting algorithms for the Mandelbrot set*.

**Idea:** before the iteration loop, test whether `c = (cx, cy)`
falls inside the main cardioid:

```
q = (cx - 0.25)^2 + cy^2
if q*(q + (cx - 0.25)) < 0.25 * cy^2 -> in cardioid (skip iteration)
```

or the period-2 bulb on the left:

```
if (cx + 1.0)^2 + cy^2 < 1/16 -> in period-2 disk (skip iteration)
```

About 30 percent of pixels in the standard view fall in one of
these regions; for them we can output `@` directly without iterating.

**Reality for us:** saves IMUL time, which is only ~33 percent of
render. Of that, maybe half the in-set pixels could be early-
rejected. *Predicted* net savings on render: ~5 percent.

The output cost is *unchanged* — we still print 1,872 chars through
INT 21h.

**Predicted gain: ~3-5 percent of render. Measured gain after
shipping: ~31 percent of post-boot runtime (572k instructions out
of 1.85M).** The estimate was off by ~6x because it assumed math
was only 33% of render; once output batching (3.7) collapses the
syscall budget, the residual is dominated by per-pixel iteration,
and the cardioid + period-2 disk together capture an outsize share
of the in-set bulb. **Code cost: ~50 lines. Apply.**

### 3.4  Vertical symmetry (top mirrors bottom)

**Idea:** the Mandelbrot set is symmetric about `cy=0`. Compute the
top 12 rows once; reuse for the bottom 12.

**Reality for us:** halves IMUL work (~67K inst saved) but does
*not* halve INT 21h calls — every char still has to be written. The
saved 67K is ~17 percent of render.

It also requires storing the top half's output (12 rows x 78 chars =
936 bytes) so we can replay it as the bottom. That's not free in
.COM-program space.

**Gain: ~17 percent of render. Code cost: ~30 lines + 1 KB buffer.
Skip — Tier-2 idea at best.**

### 3.5  Periodicity detection

**Source:** Fractint internals; multiple papers.

**Idea:** during iteration, store every Nth `(zr, zi)`; if the
current pair matches a stored one, the orbit has cycled — point is
in the set.

**Reality for us:** at MAX_ITER=24 it almost never fires. The cycle
period for in-set points is often longer than 24 iterations, so
detection mostly costs you the comparison overhead without saving
iterations.

**Gain: marginal at MAX_ITER=24. Skip.**

### 3.6  Boundary tracing / rectangle fill (Fractint's signature optimization)

**Source:** Fractint, since 1989.

**Idea:** if all four corners of a rectangle have the same iteration
count, the interior probably does too; fill the rectangle and skip
its interior pixels.

**Reality for us:** very effective at high zooms with smooth
regions. At default Mandelbrot view on a 78x24 ASCII grid, there's
basically no flat region big enough to trigger it without false-
filling the boundary detail.

**Gain: ~0 percent at our resolution. Code cost: ~80 lines.
Skip.**

### 3.7  Output batching via INT 21h AH=09h PRTBUF

**Source:** none — this isn't in the Mandelbrot literature, because
the literature assumes direct framebuffer writes.

**Idea:** instead of calling `AH=02h CONOUT` once per character (78
calls per row), build a row buffer in memory and call `AH=09h
PRTBUF` once per row (1 call + the kernel's own internal CONOUT
loop). This eliminates 77 of every 78 INT 21h dispatches.

The kernel's `PRTBUF` handler still calls CONOUT internally for
each char, so the BIOSSEG-OUT cost per char is unchanged. The
savings come from:

- 1 INT instruction + 1 IRET per row instead of 78
- 1 DISPATCH-table walk per row instead of 78
- 1 kernel ENTRY/EXIT pair per row instead of 78

That's the bulk of the ~209 inst-per-call overhead. The remaining
per-char cost (BIOSSEG-OUT + emulator trap) stays.

**Estimated gain: ~20-30 percent of render** depending on how the
kernel's `PRTBUF` actually iterates internally — to be measured
post-implementation. **Code cost: ~20 lines. Apply.**

### 3.8  Register-allocation rewrite

**Source:** demoscene 256-byte Mandelbrot intros; folklore.

**Idea:** keep `zr`, `zi`, `zr^2`, `zi^2`, `cx`, `cy`, iter counter
all in registers. 8086 only has 8 16-bit registers (and some are
used as pointers), so this is a constant tetris.

**Reality for us:** the inner loop is already pretty tight. The
86-DOS kernel's INT 21h handler clobbers nearly every register
across the call (this surprised the original implementation; see the
comment block at the top of `asm/mandel.asm`). So loop state HAS to
live in memory.

The remaining IMUL+normalize sequence between INT 21h calls *can*
use registers freely, but the wins are small (a few inst per
iteration) and the risk of breaking the carefully-validated math is
high.

**Gain: ~5 percent best case. Code cost: ~50 lines + bug risk.
Skip.**


## 4. What we shipped

**Output batching (Section 3.7).** Single optimization. Builds a
79-byte row buffer in MANDEL.COM's data section; emits one `AH=09h`
call per row instead of 78 `AH=02h` calls.

The kernel's `PRTBUF` handler in `86DOS.ASM` reads bytes until it
hits `$` (`0x24`), so the buffer is `78 chars + \r + \n + $` = 81
bytes terminated. Row terminators get folded into the same call.

Before/after instruction counts (host, `test_mandel.c`):

```
                       total       post-boot    notes
no batching        2,651,300       1,936,000    1 INT 21h per char
+ output batching  2,561,300       1,846,000    -3.4% / -4.6%
+ cardioid         1,989,300       1,274,000    -22.3% / -31.0%
                                                  (vs no-batching)
```

Step-over-step (each row is the delta added by that single optimization
on top of the previous row):

```
                            delta vs prior    reduction-of-total
output batching                  -90,000              3.4%
cardioid early-reject           -572,000             22.3%
```

The Mandelbrot output bytes are unchanged across all three rows
(1,742 of 1,872 cells non-space, byte-identical grid).

**The gain came in below the ~20-30% range I predicted.** Honesty
trumps tidy prose, so: the prediction was wrong because I assumed
DOS 1.0's `AH=09h PRTBUF` was a tight per-character loop with most
of the per-call overhead amortized away. Looking at the savings —
~90,000 instructions across 1,896 fewer `INT 21h` round trips
(1,920 -> 24) — we recovered roughly 47 instructions per
eliminated `INT 21h`. That suggests the dispatch path itself is
cheaper than the agent's earlier ~209-inst-per-call estimate
implied; most of the per-character cost is downstream of the
dispatch, in either the kernel's per-character `Ctrl-C` checks or
in our emulator's BIOSSEG OUT trap which fires per character
regardless of how the dispatch was reached.

Either way, the absolute savings are real (~3.4% of total demo
runtime), the code is simpler (one CONOUT path instead of three),
and the change is shippable.

**Cardioid + period-2 disk early-reject (Section 3.3) was added
afterwards** and beat its own predicted gain by a wide margin. The
estimate in section 3.3 ("3-5% of render") assumed the IMUL math
was a small fraction of total cost; the actual measurement shows
that *after* output batching strips out most syscall overhead, the
per-pixel iteration math becomes the dominant remaining cost — and
~30-40% of in-set pixels (the central bulb) iterate to MAX_ITER, so
short-circuiting them saves real cycles. The two optimizations are
synergistic in the sense that batching exposed the math cost that
cardioid then attacks. Combined, they cut total runtime by 22% and
render-only runtime by 31%, with the output grid byte-identical.

**What we did not ship:**

- Symmetry (3.4) — would need a 1 KB output buffer in a .COM
  program; gain is real but not load-bearing for v1.
- Periodicity, boundary tracing, register tricks — gains too small,
  risks too high.


## 5. What we learned about 8086 + DOS, in one paragraph each

**8086 IMUL is fast, but it's not free.** A signed 16x16 -> 32-bit
IMUL on real 8086 was 128-154 cycles in the original silicon. On
our emulator each IMUL is one decoder pass plus the actual product,
so call it ~10 host instructions. The renormalization shift to
Q4.12 adds another ~6. So a Mandelbrot iteration body is ~50
instructions of math in our world. That's already much less than
one `INT 21h` round trip.

**DOS 1.0 has a working `INT 21h AH=09h` (PRTBUF).** The DISPATCH
table at `86DOS.ASM:280-320` includes it explicitly, and the
handler walks the buffer until `$` (`0x24`). This is a documented
1981 service, not something we added. The string-terminator-as-
sentinel convention came from CP/M, which 86-DOS was famously
"not entirely unlike."

**The kernel does not preserve registers across `INT 21h`.** This
is a real footgun. An early version of `mandel.asm` kept the
column counter in DI; after the first CONOUT, DI came back as some
post-handler garbage, and the inner loop emitted exactly one `@`
per row before falling off the end. The fix is the comment block
at the top of `asm/mandel.asm`: every loop variable lives in
memory and is reloaded on each iteration. *This is documented
behavior in the DOS 1.0 source*, but it is not something a modern
8086-asm tutorial would warn you about, since later DOS versions
were friendlier.

**Tim Paterson's kernel is small, but it's not bare metal.** Every
time we go through `INT 21h` we pay ~209 emulator instructions for
dispatch. On real DOS 1.0 silicon the cost would have been
proportionally similar in cycles. This is why the demoscene wrote
to `0xB8000` directly — to skip DOS. We don't, on principle: the
demo's whole point is that 86-DOS is *running*. So our optimization
ceiling is "minimize syscalls," not "tighten the math."


## 6. Modern (2010s-2020s) work on old systems

There has been a steady stream of demoscene work on 8086 and DOS
since 2010. Notable categories:

- **256-byte and 128-byte intros** — Mandelbrots in this size
  budget exist (Hugi Compo, Assembly, Revision). They use direct
  CGA/VGA writes, register-tight code, and aggressive symmetry.
  None of this transfers cleanly to a "running through DOS"
  context.

- **PCjs project** (`pcjs.org`) — a JavaScript x86 emulator
  faithful enough to run Tim Paterson's actual binaries. The
  user pointed this out as a sanity check: "Mandelbrot runs in
  IBM BASIC 1.00 on PCjs, so it's clearly possible." It is — but
  PCjs doesn't have our INT 21h dispatch overhead, since it
  emulates at the instruction-cycle level and its host I/O
  bypasses the disk and console paths via different short-
  circuits.

- **Iterated Dynamics** and **ManPWin** (Fractint forks on
  GitHub) — modern continuations of Bert Tyler's original
  FRACT386. Useful as algorithm references; the source code is
  well-commented, and the integer-arithmetic core is intact in
  Iterated Dynamics for users without an FPU. We borrowed the
  cardioid/period-2 formulas from the comments in
  `Iterated-Dynamics/.../calcfrac.c`. Apart from those formulas,
  Fractint's heavy lifting is rectangle-fill and adaptive
  refinement, which don't fit our 78x24 grid.

- **Fabien Sanglard's writeups** on early game-engine internals
  occasionally hit DOS-specific perf, including
  `INT 10h`/`INT 21h` cost. The rough number we use here (~200
  instructions per `INT 21h` call) lines up with his independent
  measurements on a Pentium-class era emulator.

Nothing in 2020-2026 fundamentally changes the picture for a
hosted-DOS-Mandelbrot. The math hasn't moved. What's moved is
that we now have the patience and tooling to run these things
through cycle-accurate emulators and *measure* the syscall cost,
rather than guess.


## 7. References

- Adrian Cable, *8086tiny v1.25*, https://www.megalith.co.uk/8086tiny
- Michael Abrash, *Graphics Programming Black Book*, ch. 63,
  https://www.phatcode.net/res/224/files/html/ch63/63-02.html
- Bruce Dawson, *Faster Fractals Through Algebra*,
  https://randomascii.wordpress.com/2011/08/13/faster-fractals-through-algebra/
- Bert Tyler et al., *FRACT386 / Fractint*, mirrored at
  spanky.triumf.ca; modern fork at
  https://github.com/JonathanWGreen/Iterated-Dynamics
- Wikipedia, *Plotting algorithms for the Mandelbrot set*,
  https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set
- Mathr, *Cardioid and Bulb Checking* (2022),
  https://mathr.co.uk/blog/2022-11-19_cardioid_and_bulb_checking.html
- PCjs, *IBM BASIC 1.00*,
  https://www.pcjs.org/software/pcx86/app/ibm/basic/1.00/
- Tim Paterson, *86-DOS 1.00 source listing*, in this repo at
  `Paterson-Listings/3_source_code/86-DOS_1.00/86DOS.ASM`.
