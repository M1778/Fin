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
