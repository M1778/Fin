# Known issues

Everything here is a defect or limitation that was reproduced and written down rather than
worked around silently. Each section states whether it is fixed, open, or an operating note,
because a list that mixes them without saying so stops being useful the moment it grows.

| Subject | Status |
| --- | --- |
| `1.0.0` x86_64-linux-gnu segfaults before `main` | **Fixed in 1.0.1** (PIC on `fin_core`); 1.0.0 stays prerelease |
| `1.0.0` reports contract 3 without `--explain` | **Fixed in 1.0.1**; 1.0.0 stays as published |
| `-lLLVM` named unconditionally on every link | **Fixed in 1.0.1** |
| A float-to-int cast answered differently per architecture | **Fixed**, and recorded: the defect was in its own first fix |
| macOS reports a NUL in source as a syntax error | **Open**, observed, not root-caused |
| The stage invents type errors after `parse_float` | **Open**, pinned by a test, documented |
| A `&char` parameter cannot be indexed with a literal | **Open**, a language gap; worked around in `fin-ssg` |
| The Windows CRT's `"ab"` is not an atomic append | **Open**, documented in `lib/std/fs.fin` |
| A 64 KiB stack buffer plus a thread aborts on Windows | **Open**, worked around in `logg` |
| `macos-15-intel` runner stalls with no log | **Operating note**, cancel and re-run |

**Read the status before the detail.** Three are fixed and those fixes ship in 1.0.1; five are
open limits a caller has to design around, three of them Windows-specific; and one is a runner
problem that has nothing to do with Fin.

---

# Known release defect: x86_64-linux-gnu `finc 1.0.0` segfaults before `main`

Status: **confirmed, reproduced, root-caused**. Affects the published `v1.0.0`
release. Found 2026-11-20 during post-release artifact verification.

## Symptom

The glibc Linux x86_64 artifacts crash on `--version`, before `main` runs:

```
$ tar -xzf finc-1.0.0-x86_64-unknown-linux-gnu.tar.gz --one-top-level=gnu
$ ./gnu/bin/finc --version
Segmentation fault (core dumped)          # exit 139
```

Affected assets (all three carry the same `bin/finc` bytes):

- `finc-1.0.0-x86_64-unknown-linux-gnu.tar.gz`
- `finc-1.0.0-x86_64-unknown-linux-gnu.deb`
- `finc-1.0.0-x86_64-unknown-linux-gnu.rpm`

Not affected, measured: `x86_64-unknown-linux-musl` (runs, `finc 1.0.0
(contract 3)`), and the aarch64/Windows/macOS artifacts (different link path).

## Fault location

Not in Fin. The crash is inside libLLVM **during dynamic linking**:

```
0x00007fffee1591a3 in ?? () from /usr/lib/libLLVM.so.22.1
#1 ld-linux-x86-64.so.2
```

i.e. it dies while the loader is running libLLVM's initializers, before
`finc`'s own `main` is entered.

## Mechanism (proved, not inferred)

The binary has **31 `R_X86_64_COPY` relocations**; 25 of them land inside the
`PT_GNU_RELRO` segment. A COPY relocation reserves writable storage in the
executable for a data symbol owned by a shared library, and the dynamic loader
then copies the library's initial value over it. libLLVM's later static
initializers must write those same globals.

Proof that the ELF layout is the cause: **neutralising the `PT_GNU_RELRO`
header (`p_type` → `PT_NULL`) in a byte-copy makes the same binary run.**

```
$ ./relro-off --version
finc 1.0.0 (contract 3)          # exit 0
```

Same bytes otherwise; only the RELRO program header differs.

## Why CI was green and a user's machine is not

The published binary was linked on `ubuntu-24.04` against that image's
`libLLVM-22` package. A locally linked build (this host, Arch, LLVM 22.1.8)
contains the same COPY relocations for the same four
`llvm::OptimizationLevel` globals, but links them **outside** the RELRO range:

| binary | COPY inside RELRO | COPY outside |
| --- | --- | --- |
| published (CI-linked) | 25 | 6 |
| local build | 20 | 9 |

So a COPY relocation here is only correct when the executable and the runtime
`libLLVM.so.22.1` agree on where those globals live. They agree on symbol name
and on size (both export 8-byte `OptimizationLevel` objects — verified), but not
on placement relative to RELRO, because the two `llvm-22` packages were built
with different link layouts.

This is a latent portability trap in linking against a monolithic shared
`libLLVM`, not a bug in a particular flag: `-z relro`, `-z relro,-z now`, and no
flag at all all produce a working binary when built and run on the same host.

## Candidate fixes (none applied yet)

1. **Build position-independent objects** (`-fPIC` / `CMAKE_POSITION_INDEPENDENT_CODE ON`).
   COPY relocations exist only for non-PIC references to shared-library data;
   PIC accesses them through the GOT instead, which removes the class of defect
   entirely. Largest blast radius: rebuilds everything, may perturb performance.
2. **Link LLVM statically for glibc Linux** (the musl lane already does this and
   is unaffected). Removes the shared-library ABI dependency, costs archive size.
3. **Pin `llvm-22` to the exact package build** used for the release and state
   that restriction in INSTALL.md. Smallest change, weakest guarantee: it makes
   the trap documented rather than gone.
4. **Ship only the musl binary for glibc Linux users.** Works today, but throws
   away the distro-package lanes.

Recommendation: (1), verified by building the release lane with PIC and running
the artifact on a *different* host than the one that built it. (1) and (2)
compose; either should be proven on the actual release runner before the next
tag.

## Test gap this exposed

`release.yml` proves an artifact by running `--version` **on the machine that
built it**, where the trap cannot appear. Any fix must add a check that runs the
staged binary and compiles a program in a container or image different from the
build image — otherwise a green release keeps meaning "it works here".

---

# Open: macOS reports a NUL in source as a syntax error

Status: **observed, not root-caused**. Affects macOS only.

`Soundness_SourceEncoding.TheUnrecognisedByteMessageNamesTheByteItRejected`
asserts that a NUL byte in source is named by the lexer's catch-all
(`unrecognised byte in source: '\x00'`). On macOS the same input instead
reports `syntax error, unexpected SEMICOLON, expecting IDENTIFIER` at the
column *after* the NUL, so the byte is never named.

The ESC half of the same test passes on macOS, so the naming path
(`reportLexerError`) works; the byte is not reaching it. The likely cause is
the flex-generated scanner treating NUL as end-of-buffer on that platform,
which would mean the catch-all rule never matches. Not confirmed.

History: the test passed on macOS in run `41770346027` (2026-11-04) and fails
in run `44791798724` (2026-11-20). The diagnostics catalogue landed between
them, but nothing in it reaches the lexer, so the correlation is unexplained
rather than causal. Linux is unaffected and still asserts the behaviour.

The macOS half now skips with that reason named instead of asserting a
behaviour the platform does not have. **Deleting the skip is the fix landing**
-- restore the plain assertion once the lexer reaches its catch-all there.

---

# Open: the stage invents type errors after `strings::parse_float`

Status: **confirmed, isolated to one file, introduced by the 1.0.1 stdlib work.**

```
import { printf } from stdio;
import strings;
fun main() <noret> { printf("x"); }
```

- Reference: exit 0, object emitted, no diagnostics.
- Stage2/Stage3: exit 1, **15 type errors**, attributed to the importer's file
  at a line number that does not exist (line 540 of a three-line program).

## Cause, isolated

`lib/std/strings.fin` as it stands in this release. Reverting **only** that file
to its previous contents clears the defect; every other file — including the
project header added to all 348 sources — stays as it is, and the errors stay
gone. Restoring `strings.fin` brings the 15 errors back. Measured both ways
with the bundled stdlib path, not with `--fin-libs`.

The new code in that file is `parse_float` and its helpers. A first hypothesis
blamed the header (the defect appeared in the same commit range); that was
tested by stripping the header from `stdio.fin` and from `strings.fin` in turn,
and it is not the cause. A second hypothesis blamed `stdio`'s new `read_line`;
reverting `stdio.fin` alone does not clear it either.

## Scope measured

`stdio` + `networking` → 15 errors; `stdio` + `strings` → 15; `stdio` + `json`
→ 69; `json` + `networking` → 1; `networking` alone → 0. The messages name
`IOError` and `Outcome<double>`, which `stdio` and `json` declare, while the
second import is `strings` — so the stage is resolving declarations across
module boundaries it should keep separate, and reporting the result against
the caller's file.

Pinned by `KnownDefect_StageCoverage.ImportingFromStdioThenAModuleInventsTypeErrors`.
The two driven-corpus agreement tests for `importing.fin` fail for this reason
and are allowed to.

Not root-caused. A future reader should start at how the stage carries a
module's declaration list across files: the bogus line number suggests
positions from one module's AST are rendered against another module's source.

---

# Known release defect: `finc 1.0.0` reports contract 3 and lacks `--explain`

Status: **confirmed on the published `v1.0.0` artifacts**. Found 2026-11-21 by the VS Code
extension work, which implements an "explain this diagnostic" action.

## Symptom

`docs/finc-interface-contract.md` documents `--explain <code>` and `--explain list` as part
of contract 3, and `finc --version` on the v1.0.0 artifacts answers `finc 1.0.0
(contract 3)`. The flag is not there:

```
$ finc-1.0.0-x86_64-unknown-linux-musl/bin/finc --explain E0301
error: unknown option '--explain'
   = help: run `finc --help` for usage.

$ finc-1.0.0-.../bin/finc --explain list
error: unknown option '--explain'
```

`finc 1.0.1` has it:

```
$ finc --explain E0301
E0301 (error): a name was used that nothing in scope declares
```

## Why this matters more than a missing convenience

The contract integer is the one thing a consumer is told to branch on, and this release
demonstrates the failure mode it exists to prevent: a tool that reads `contract 3`, offers a
feature the contract promises, and gets `unknown option` from the compiler.

Nothing about v1.0.0 is being repaired — see the note at the top of this file. What this
entry adds is the reason **1.0.1 is the first release a tool should target**, and a concrete
instance of the rule that a consumer should treat `--version` as a claim to verify rather
than a fact to trust. The extension was written to probe `--explain` before offering the
action, which is the behaviour to copy.

## Test gap this exposed

Nothing asserted that the flags named in the contract exist in a binary that reports the
contract number. `tests/test_cli.cpp` covers `--explain` against the current build, which is
correct and does not catch this: the defect is in the *released* artifact, produced by a
commit whose CMake version had not moved while the feature was absent.

The check that would have caught it is the release pipeline comparing the flags in
`finc --help` against a list the contract names. That is not implemented; this entry records
it as a gap rather than implying coverage exists.

---

# Known compiler limitation: a `&char` parameter cannot be indexed with a literal

Status: **confirmed, minimal repro**, found 2026-11-21 by fin-ssg's macOS fix.

## Symptom

The same index lowers on an array and is refused on a pointer parameter:

```fin
// refused
priv fun helper(info: &char) <int> {
    return cast<int>(info[4]);
}
// error: codegen: this index expression is not lowered yet

// accepted
priv fun helper(info: [char]) <int> {
    return cast<int>(info[4]);
}
```

`&info[0]` on an existing array works, which is why the original code -- which read
`info[24]` where `info` had come from `let info <[char]> = new [char, 256]{}` -- compiled and
a refactor that extracted the same expression into a helper taking `&char` did not.

## Why it matters

It pushed a real fix into a worse shape. The cleanup that prompted it wanted to name a
`struct stat` offset once instead of twice, and the natural signature for that is
`(info: &char)`. Taking `[char]` instead works and is what the library now does, but it
means a pointer cannot be treated as an array for indexing, which is the opposite of what
`&info[0]` → `info[24]` implies.

## Not root-caused

No test pins it yet, because pinning a refusal makes it normative and this reads like a gap
rather than a decision. The route in is the index-expression lowering path: find where an
index on a `[T]` is handled and check whether a `&T` base reaches it at all, or is rejected
earlier for having no length recorded.

Worked around in `M1778/fin-ssg` (`src/vendor/platform.fin`), with a comment naming the
compiler rather than presenting the array parameter as a design choice.

---

# Known compiler limitation: `-lLLVM` was named unconditionally (fixed in 1.0.1)

This one is recorded because it shaped three ecosystem repositories before it was fixed, and
because the workaround outlived the bug.

`finc program.fin -o program` passed `-lLLVM-22` on every link. A reference-compiled object
references no LLVM symbol -- `nm -u hello.o` shows `printf` alone -- so the flag bought
nothing and made the link fail on any machine without the LLVM *development* package.
`-Wl,--as-needed` did not help: it decides whether to record a dependency after the linker
has already found the library.

Fixed in `1c0c658`: the library is named only when an object being linked mentions an LLVM
symbol. Repositories that added `FIN_LDFLAGS` overrides can drop them; the release notes for
1.0.1 say so.

---

# Known limitation: the Windows CRT's `"ab"` mode is not an atomic append

Status: **confirmed on `windows-msvc`**, found 2026-11-21 by the `logg` hardening.

Four threads appending to one log file through `fopen(path, "ab")` lost 6 of 80 records while
every call returned success. The CRT implements append as seek-to-end followed by write, so two
threads that seek before either writes land on the same offset and one record overwrites the
other. The file is well-formed; it is simply missing lines.

A logger that silently drops records under concurrency is the one failure a log cannot afford,
because the missing lines are exactly the ones that would have explained what happened.

## What works instead

`FILE_APPEND_DATA` in a `CreateFileA` disposition is an atomic append on Windows, and
`O_APPEND` is atomic on POSIX. `logg` uses the former; the workaround is recorded here because
`lib/std`'s `fs::append_string` uses `fopen(..., "ab")` and inherits the same behaviour.

## Does this affect `fs::append_string`

Yes, on Windows, for concurrent appenders. It is documented in `lib/std/fs.fin` beside the
function; the note points at `stdio::std`'s `File` operations for a caller who needs atomic
append. Making `append_string` itself atomic means either a Windows-only `CreateFileA` path or
an `O_APPEND` path, which is a signature-preserving change worth doing and not done here.

## Not fixed in this release

Changing the append path is a behavioural change to a shipped stdlib function on a platform the
release cannot test locally, and 1.0.1 is a patch. Recorded rather than risked.

---

# Known limitation: a large stack buffer plus a spawned thread overflows the Windows stack

Status: **confirmed on `windows-msvc`** by the same work.

A 64 KiB `[char]` buffer declared as a function local overflows the default Windows thread
stack (`0xC0000409`, STACK_BUFFER_OVERRUN fail-fast) when that function also spawns a thread.
The same buffer is fine single-threaded, and the same code is fine on Linux and macOS.

The interaction is the surprising part: the buffer is not on the spawned thread's stack, so the
failure is not the obvious one. Presumably the thread's stack reservation interacts with the
parent frame's size in the `CreateThread` path; that is a hypothesis, not a measurement.

Workaround: allocate the buffer on the heap (`new [char, n]{}`) rather than as a local, which
is what `logg` does. Recorded because the failure mode is a fail-fast abort with no diagnostic
naming the buffer, so a reader hitting it has nothing to search for.

---

# Fixed: a float-to-int cast whose answer differed by architecture

Status: **fixed**, and recorded because the defect was in a *fix* and only a second architecture
caught it.

## What happened

`cast<int>(3.9)` was not lowered at all (issue #120). The first version of the lowering clamped
the float to `2^(N-1)` and then converted it -- which is still undefined, because `2^31` is one
past INT32_MAX. The undefined answer differed by target:

```
x86_64    cast<int>(1e38)  ->  -2147483648
aarch64   cast<int>(1e38)  ->   2147483647
```

`linux-x86_64` was green. `linux-arm64` failed my own new test, which had been written from the
x86_64 answer.

## The rule

`fptosi` and `fptoui` are undefined outside the destination's range and for NaN. Clamping to the
boundary does not help, because the boundary itself is out of range -- the largest double below
`2^31` is in range and `2^31` is not.

The lowering now compares against the power-of-two threshold (exactly representable in every
float type), selects an INTEGER constant at each edge, and only converts a value the comparisons
proved in range. `nan` answers 0.

```
cast<int>(1e38)                     ->  2147483647
cast<long>(1e38)                    ->  9223372036854775807
cast<int>(-1e38)                    -> -2147483648
cast<ulong>(-1e38)                  ->  0
cast<int>(2147483647.0)             ->  2147483647        (in range, exact)
```

## Why it is written down

Two lessons that cost an afternoon:

1. **A clamp to the boundary is not a guard.** The boundary is in the undefined set. The guard
   has to select the answer, not the input.
2. **A test written from one platform's answer agrees with that platform's bug.** The x86_64 run
   was green and the aarch64 run was the evidence. This repository runs both for exactly this
   reason; a single-architecture matrix would have shipped it.

---

# Operational note: the `macos-15-intel` runner is unreliable, and the release gate is all-or-nothing

Status: **observed across four release attempts on 2026-11-21**, not a compiler defect.

## Symptom

The `x86_64-apple-darwin` lane starts, gets through `Set up job`, `checkout` and
`setup-toolchain`, then stops making progress. The job reports `in_progress` with **no step in
progress** and `completed_at` null, and its log endpoint answers:

```
<?xml version="1.0" encoding="utf-8"?><Error><Code>BlobNotFound</Code>
<Message>The specified blob does not exist.</Message>
```

That last part is the diagnostic that matters. A job that is genuinely running has a live log;
a `BlobNotFound` log with no active step means the runner is gone and GitHub has not yet
noticed. Waiting does not help -- one attempt sat for over forty minutes.

The same lane succeeded on `44808655283`, so the label is valid and the code is fine. It is the
runner.

## What to do

Cancel the run and re-run it. The tag is unchanged, so the artifacts are identical; nothing
about the release is being decided differently. Two reruns passed after one such stall.

## Why this is written down

The release gate is `publish`, which needs all eight build lanes. One flaky runner therefore
blocks the whole release, and the failure looks like a build problem: `publish` is marked
failed, and the obvious reading is "something is wrong with the macOS x86_64 build".

It is worth distinguishing these before investigating the compiler:

- a lane with an **active step** and no log access is still building (logs are uploaded at the
  end for some steps);
- a lane with **no active step** and `BlobNotFound` is dead.

A future improvement would make `publish` name the missing artifact rather than failing with a
generic error, and offer `workflow_dispatch` a way to publish from the six lanes that succeeded
when one runner is down. Neither is implemented; this note is the workaround.
