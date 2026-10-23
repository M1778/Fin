# Handoff — Fin compiler, v1 production readiness

Updated **2026-10-23** from local `HEAD` @ **`1f7afd9`**, with the local changes
listed below still uncommitted. Worktree branch `issue-101-asan-type-leaks` is the
trunk line; `origin/master` is the published truth (see Branches).

This file is the **single entry point** for the next agent — self-contained by design, including
for a fresh subagent with no conversation history. It states what is done, what blocks a
production release, the traps that cost real time, and the order of work. Every number here was
measured on the date above; **re-measure before quoting**, and re-read `AGENTS.md` (canonical)
when anything here disagrees.

## Mission — what "production-ready v1" means

The user explicitly requested an actual **1.0 production release** on 2026-10-21.
The local candidate identifies as **1.0.0 (contract 3)**. It is not yet a
validated or published stable release; the version number does not satisfy the
release gates. The acceptance conditions are:

1. **Green required CI lanes** on `master`: linux-x86_64, linux-arm64, macos-x86_64, macos-arm64,
   windows-x86_64, windows-arm64 (published lanes are green with platform skips — see Gate).
2. **`v1.0.0` tagged and `release.yml` green** (six-platform + Alpine musl matrix; the tag is the
   release gate, not a formality).
3. **No silent platform gaps**: every shipped `lib/std` module links and its tests run on the
   required lanes. Cross-platform claims are backed by lanes, never by assumption.
4. **Contract v3 enforced on every lane** (the contract corpus replays green everywhere).
5. **Standing language goal**: all runnable normative samples compile and run; every remaining
   refusal is named and owned (held ruling or documented blocker) — no unexplained red.
6. **Docs gate green** and current (`check_docs.py`), ADR index current, this handoff current.
7. **Owner decisions recorded** for the open rulings below (none settled unilaterally).

Everything in "Backlog" is post-v1 and does not gate the tag.

## Read-first order (for a new agent)

1. `AGENTS.md` — working rules, invariants, vocabulary (canonical; this file complements it).
2. `tests/samples/*.fin` — **the language specification** (ADR 0008): each `//@` header is the
   testable expectation (`ok`, `error`, `unimplemented`). 75 samples today.
3. `docs/agent-guide.md` — how to write Fin programs (topic-linked).
4. `docs/finc-interface-contract.md` — the machine contract (exit codes, JSONL, stdout reserved).
5. `docs/adr/README.md` — one line per ADR (58 filings, 0024 reserved); then the ADR behind the
   area you touch.
6. `CONTEXT.md` — terminology (also lists planned concepts; not a feature-support reference).
7. `docs/reflect-derive-plan.md` — live plan/status for the reflect/derive family (§6).

## Verified state (local 2026-10-23; remote measurements 2026-10-21)

**Compiler:** local candidate `finc 1.0.0 (contract 3)`, LLVM 22 backend enabled. Fresh census:
49 modules in `lib/std/`; 75 samples recursively under `tests/samples/`, including
48 directly in that directory. Sample expectations remain authoritative.

**Published CI:** run **39486020060**, `master` @ **`1f7afd9`**, completed green on
all six required platforms plus build.sh, performance, and sanitizer jobs. This
predates the uncommitted changes below. Five logging tests and two filesystem
tests were skipped on Windows in that revision; green CI did not prove those APIs.
The prior complete run **39430006292** at `c58c5f3` was also green.

**Remote readback (2026-10-23):** `master` still points at `1f7afd9`;
`v1.0.0` is absent and the historical `v0.4.0` remains `14547b4`. No releases are
listed. More recent successful CI runs belong to PR/dispatch revisions
`c259e5a` (run 39795608686) and `9852289` (39741770240), not this uncommitted
candidate. They are not evidence that the candidate's six native lanes passed.

**Candidate CI follow-up (2026-10-24):** `cc7f166` is pushed only to
`issue-101-asan-type-leaks`; run 40026847877 passed Linux x86_64/arm64 and macOS
arm64, while both Windows lanes failed. Logs identified the bare `ws2_32.lib`
linker input and `sync.fin`'s unsupported `#c_ifndef` cleanup guard. The follow-up
uses `-lws2_32` and supported `#c_ifdef`/`#c_else`, with regressions. Its local
selector/callback/sync gate passed 27 tests; docs passed 74 examples. Corrected
native CI is required before using candidate artifacts as six-platform proof.
The initial macOS x86_64 and sanitizer jobs were still running at this update.

**Release:** remote tag **`v0.4.0` already exists** at **`14547b4`**. Its release run
**39419160854** failed on musl, both Windows platforms, and both macOS platforms;
publish was skipped. `gh release list` returned no releases. The earlier handoff's
claim that the tag had not been cut was stale. Subsequent commits changed the
musl filesystem path and packaging, but the existing tag does not include them.

**Current validation:** the final Debug candidate passed **3,045 tests, 57 skipped,
zero failures** (3,102 checks, 319.47 seconds). An independently built **Release**
candidate passed the same 3,102 checks (259.25 seconds) after receiving its expected
sibling `lib/std` layout. Both reference builds bootstrapped Stage2/Stage3 to the
same byte-identical result (SHA-256
`1415dbd213825a98a30594c943b6d34bbf16f56904750d7412dcfb78d09a276c`).
The final standalone sanitizer compiler suite passed **2,814 tests, 299 skipped,
zero failures** (3,113 checks, 866.76 seconds), with ASan leak detection and UBSan
halt-on-error enabled. The focused sanitizer follow-up also passed **63 tests,
48 missing-stage skips, zero failures** (111 checks, 51.95 seconds). The sanitizer
tree has no stage executables; stage allocator checks are separate.

**Installed and packaging proof:** installed Reference, Stage2, and Stage3 compiled
and ran Collection/Channel programs at `-O0` and `-O2` from a foreign directory
with `FIN_LIBS` unset. All **147 module type-checks** passed (49 modules × three
compilers). Installed Debug and Release references replayed **six contract corpus
cases** without changing goldens, preserved explicit empty library pins, and
refused JSON plus executable-output mode. The optimized installed reference passed
**74 doc examples** (six marked fragments skipped). Site checks passed 20 module
anchors and 37 search entries. Final optimized GNU/musl portable archives and a
Debian package were generated locally; archive bytes, checksums, architecture and
Debian extraction/runtime checks passed. RPM tooling in an isolated Alpine container
built/unpacked the final optimized host-glibc package whose extracted runtime
passed. These artifacts are local rehearsals, not published release assets or native Ubuntu/Fedora dependency compatibility proof.
Native Windows/macOS installer execution remains unverified.

**Static musl rehearsal:** Alpine 3.24 with LLVM 22 static components passed
**2,802 tests, 300 skipped, zero failures** (3,102 checks, 135.94 seconds), plus
installed documentation, architecture, and no-dynamic-loader checks. Missing stages
are among its explicit skips. The static compiler also compiled and ran the
installed-library probe on the glibc host. Source mounts stayed read-only and Conan
user-preset writes were disabled. This local rehearsal does not replace the native
release workflow on the published revision.

**Empty-handle runtime follow-up:** broader installed lifecycle probes found
additional stage leaks in Collection (**512 bytes / 32 allocations**) and HashMap
(**6,144 bytes / 384 allocations**). No-initializer dynamic arrays were represented
by hidden 16-byte allocations that escaped through shared handles. They now lower
to null handles, with null-safe length and deletion. Three new regressions first
failed on both stages and now pass. All **144 allocator-sanitizer runs** over
24 ownership/lifecycle programs across Reference, Stage2, and Stage3 at `-O0` and
`-O2` passed; only the intentionally pinned lexical-lifetime program disables leak
detection. The final Debug/Release full gates and fixed point above include this
follow-up; the focused ownership gate passed **72/72 tests** (16.79 seconds). CDK,
IR, scanner, smart-pointer, mutex and channel lifecycle probes also passed installed
allocator checks.

**Self-hosted aggregate ownership repair (2026-10-23):** the candidate eliminates
this probe's **1,055,744-byte / 1,031-allocation** baseline leak. Struct bindings
have reusable entry-allocated heap homes, returns snapshot independently, and the
caller releases detached representation after adoption. Temporary receiver homes
survive later statements. Nested values and fixed-array fields copy recursively;
array elements own escaping representation until their buffer is deleted. Explicit
heap objects retain separate ownership. Semantic destruction remains separate from
representation release; loop exits, moved bindings, and deferred cleanup are covered.
See `docs/selfhost-ownership-plan.md` for the design and proof boundaries.

The aggregate candidate passed **3,036 tests, 57 skipped, zero failures**
(3,093 checks, 273.30 seconds) with byte-identical scratch Stage2/Stage3. All
**63 ownership regressions** passed across Reference, Stage2, and Stage3 at `-O0`
and `-O2`; **126 sanitizer-linked emitted-program runs** passed, with leak checks
excluded only for the intentionally pinned case. The sanitizer links check
allocator errors and leaks, not arbitrary emitted memory accesses. This is not
a claim that the compiler process itself is leak-free, nor a substitute for
native platform CI or the remaining release gates.

The completed resource-cleanup slice passed 12 focused tests, including five
LeakSanitizer cases. CDK and scanner lifecycle probes passed all three compilers.
Independent review found no concrete defects in those changes. Performance
benchmarks compile and produce correct outputs; current WSL2 timings are not
comparable to the checked-in native-Artix baseline.

**Uncommitted work in this checkout:**

- Native ABI definitions (`FIN_TARGET_WINDOWS`, `FIN_TARGET_DARWIN`,
  `FIN_TARGET_LINUX`, `FIN_TARGET_X86_64`, `FIN_TARGET_ARM64`) use existing
  `#c_ifdef` in both the C++ and self-host preprocessors, including imports.
  ADR 0051 records their target-only scope; ADR 0017 remains unchanged.
- `fs::std` uses target-selected POSIX dirent layouts and public `readdir`,
  plus Win32 directory enumeration and atomically created random temporary
  directories on Windows. Windows paths preserve drive-relative semantics and
  multibyte code-page characters. Directory record lengths decode unsigned bytes,
  including names long enough to set the high bit. Windows file offsets use
  `_fseeki64`/`_ftelli64`.
  Its two Windows test skips are removed. Narrow Windows paths use the system
  code page; UTF-8 and extended-length paths are not promised.
- `log::std` uses `_putenv_s` and `fprintf(__acrt_iob_func(2), ...)` on Windows.
  Its five Windows skips are removed; text-mode stderr has an exact byte pin.
  Timestamp buffers are freed after each emission: a ten-emission LeakSanitizer
  probe changed from 189 leaked bytes in nine allocations to no leaks.
- `copy_file` now preserves embedded NUL bytes, including same-file copying.
  Reads keep the byte count from their own stream and refuse short reads; copies
  check write and close results. Memory remains proportional to the source size.
- The hash implementation already used 64-bit Fin `ulong` and LLVM `i64`.
  Commit `6134ebf` had fixed the four test formats to `%llu`; the remaining
  normative sample now does the same. ADR 0058 records platform-stable hashes.
- Contract replay normalizes matched case paths on both sides, including
  JSON-escaped Windows separators and drive-letter case. Regeneration uses
  the same rule. Corpus expectation files are unchanged.

Windows branch object-symbol and C-shim behavior probes supplement Linux tests;
they are **not native Windows execution**. Apple and Microsoft declarations were
checked for symbol names, record layouts, and parameter widths. Native CI on the
final revision remains required.

## GATE — remaining release work

### G1 — land and verify the actual fixes

Review the local diff and run all six required lanes on the reviewed revision.
After explicit git authorization, use a candidate branch for CI and dispatch
`release.yml` on that branch with `publish=false` for the packaging rehearsal.
A tag dispatch publishes even with `publish=false`. Pushing documentation to
`master` also automatically deploys GitHub Pages.
The five `Soundness_Log` and all `Soundness_Fs` tests must execute on Windows.
No new platform skips may substitute for the native proof. Linux stage agreement
must compare stdout and stderr from the current reference, stage2, and stage3.

The original B1/B2 diagnoses are historical: `getdirentries64` was replaced before
this session, and the apparent hash truncation was a C printf-format mismatch,
not 32-bit Fin arithmetic. Do not reintroduce backend/type changes to fix either.

### G2 — prove 1.0 release packaging

`v0.4.0` points at an older failed revision. Preserve it; the requested 1.0 target
uses a new version and tag. `release.yml` must pass its entire platform matrix,
including Alpine musl, on the intended sources. Finish the branch rehearsal
before pushing `v1.0.0`: `install.sh` immediately selects the newest remote
semver tag, independently of whether release packaging has succeeded.
Commits, pushes, tag changes, releases, and deployment are not authorized by this
handoff; follow the current user's instructions and canonical `AGENTS.md`.

### G3 — finish the combined runtime proof

Windows implementations for sync/networking, time, environment, process, stdio,
and CLI are present. Linux regressions and targeted C ABI probes pass; native
Windows/macOS execution remains required. Foreign function callbacks now bridge
Fin closures to native code pointers and explicitly refuse unsupported captures
and signatures. Array assignment evaluates its receiver and RHS once, and debug
locations remain valid across nested/generic emissions.

Container, CLI, Stream, scanner, HTTP, CDK, and IR storage now have explicit
cleanup paths. The owner-approved smart-pointer repair retains counters until
all weak handles release them; `release()` still drops one strong reference.
ADR 0039 records the amendment and remaining alias/ownership constraints.

Character-array storage, contextual widening, array-parameter cleanup, generic
floating-point array fields, effectful dereference, global compound assignment,
and global/local generic receiver layout now pass runtime regressions. Nested
global-name shadowing was also corrected using existing scope machinery. An
array read re-evaluated its effectful root in the reference compiler; it now
retains the first address and passes both optimization levels. The implicit
self-hosted aggregate ownership repair above now has public value-copy and
emitted-program leak regressions and has passed its final expanded Debug/Release
Linux gates, including the empty-handle follow-up.
Inspect the sanitizer job's own result: CI currently marks it optional, and some
new lifetime tests only exist in its Linux configuration.

### G4 — ecosystem provenance and open rulings

After the compiler revision stabilizes, fin-sdk must be re-vendored and Linf
re-pinned with their provenance/self-check gates green. The earlier recorded
versions were fin-sdk 3.2.0 and Linf 3.0.0; those repositories were not re-verified
for this measurement. The owner rulings below remain open.

The user additionally authorized work in `/home/M1778/finn` and
`/home/M1778/finn-registry`, prioritizing existing gaps and performance. Those
are separate working copies and do not authorize publishing or deployment.
The registry's Docker-only dependency-install rule received a specific exception
for locked dependency installation in an isolated `/tmp` copy for testing.

Both sibling patches are applied and uncommitted:

- **finn:** contract 3 builds validate a staged executable before replacement;
  `run` forwards literal arguments and exit status. `test` compiles and executes
  sorted standalone tests, with frontend-only behavior available through
  `--check`. `install` uses the cloned manifest and dependencies, installs
  atomically, and preserves the old executable on failure. Package/dependency
  names and symlink write paths are checked before modifying files. All **189
  tests**, formatting, and Clippy pass. Real-finc tests cover valid installation,
  failed replacement, and preservation of external files when a package contains
  malicious symlinks. The actual sibling sources were tested; the earlier `/tmp`
  source copy is stale. Native Windows execution remains unverified.
- **finn-registry:** dashboard queries use an indexed owner join instead of an
  unbounded `IN` list, and group versions once instead of rescanning all rows per
  package. The regression first reproduced local D1's HTTP 500 at 101 packages,
  then passed. All **426 tests**, typecheck, lint, Node build, and Worker build
  pass. The isolated tested copy matches the original three changed files. No
  manifest/lockfile changes and no deployment.

## Local build and verification

```bash
./build.sh                                   # pinned LLVM major (from CMakeLists.txt)
cmake --build build -j2                      # rebuild before any gtest run after .cpp edits
ctest --test-dir build --output-on-failure   # re-measure count and duration
rm -f *.o tests/samples/*.o tests/samples/stdlib/*.o
uv run --no-project tests/tools/check_docs.py --finc build/finc
```

Per-sample: `build/finc -c <sample>`. **Never** rebuild while a `ctest` run is in flight.
A backend-disabled build proves nothing — always verify executable behavior.

## §8 Bookings and reserved ADR numbers

- **ADR 0024 is reserved** for the wave-4 `$type` ruling (design-and-plan-only; researched,
  never written). Do not take 0024 for anything else.
- **Booked (ADR 0026, measurement):** a derived pointer where a base pointer is expected stays
  refused. Measured across the corpus (then fifty samples): **no sample passes a derived pointer
  where a base pointer is expected** — no witness, so no rule; `&&Derived` → `&&Base` is almost
  certainly wrong (it would let a `Base*` be stored through a `Derived**`). Re-measure before
  touching this; a witness is what would make it ruled.

## Held rulings — do not touch without an owner decision

- Struct `==` is **declared-only, never synthesized** (ADR 0036; `deeptest4.fin` stays refused).
- `any` boxes with **value semantics** (ADR 0034 amendment; `useful_macros.fin` builds).
- Vocabulary is frozen: blame (not assert/throw), `blame m1778;` (not todo), compiler component /
  grant `#[use(...)]`, prototype `{K,V}`, `@special` function with `@call`, nullable `?` +
  denullify `e?`, `readonly`, `pub:`/`priv:`.

## Open questions — brief with evidence, do not settle unilaterally

- **`fields_of` / `FieldInfo`** — blocked on a runtime owner for the field-name string
  (allocate/answer/free). ADR 0057 + `lib/std/reflect.fin` header.
- **`derive_eq` / `derive_encode`** — gated on the **R3** owner decision (plan doc §4).
  R1 (`$type` turbofish sample) untouched.
- Enum representation and nullable layout are settled by ADR 0041 and ADR 0040
  amendment NL-A; `enums.fin` and `nullifier.fin` build and execute.
- **Wave-4 compiler API** (`@implements`, `@defined`, `@Alloc`…) — plan before code;
  `docs/compiler-api.md` separates designed from implemented (implemented operations live in
  `src/semantics/CompilerApi.cpp` + `src/semantics/impl/Analyzer_CompilerApi.cpp`).
- **Carried, not re-verified in this sweep:** explicit `delete &field` on a field with a
  destructor can double-run with scope-exit cleanup (ADR 0016's revisit note). No corpus program
  hits it; confirm before changing ownership rules.

## Backlog (post-v1, in rough priority)

1. **fin-sdk re-vendor** after `master` stabilizes (log.fin/fs.fin changed): run `vendor.sh`,
   restamp `MANIFEST.md`, MINOR bump → 3.3.0; provenance CI must go green. Then Linf's
   self-check re-pins via the bundle's MANIFEST.
2. **Linf rule 5 (`fmt-drift`)** — `finfmt --check` is now drift-truthful for indentation/tab/
   trailing-WS (exits 1 with a diff; intra-line spacing deliberately skipped) → the rule is
   *implementable*, pending an owner ruling. Remaining Linf stage: `finn` packaging.
3. **#50 Linguist epic — in-repo slices need no new-repo allowance:** item 3 (shared
   `fin-keywords.json` from `lexer.l` → Pygments lexer + Chroma XML + highlight.js `fin.js`,
   wired to the docs site, which already has `highlightFinCode()`), item 8
   (`docs/linguist-evidence.md`). Repo-creating slices (`fin-vscode`, tree-sitter, `finls`,
   `awesome-fin`, `fin-examples`) need explicit per-repo allowance, same as fin-sdk/Linf got.
4. **pyprototype Dependabot** (2 alerts: medium pytest, low Pygments in `pyprototype/uv.lock`) —
   owner declined the bump earlier; hygiene item, re-offer rather than act.
5. **Branch hygiene decisions (owner):** delete obsolete `gh-pages` (Pages now workflow-mode);
   retire or land `docs/agent-guide-ci` (rehearsal branch, 7 ahead); inspect
   `issue-107-symbol-brace-init` — content probe shows the trunk still has 7 `slaveof` sites
   against the branch's "drops at 23", so it plausibly carries real unmerged work.
   `issue-106-adr-checks`/`issue-108-fin-sentinel` are substantively landed on the trunk
   (re-authored, so strict `git cherry` patch-ids differ) — verify before deleting.

## Branches — how to read them

- Default branch: **`master`**. Push with an explicit refspec from the worktree branch:
  `git push origin issue-101-asan-type-leaks:master` (non-force; push only when the task says so).
- A bare `git push` from the worktree pushes the *branch's own* upstream
  (`origin/issue-101-asan-type-leaks`), **not** `master` — this bit once; always verify with
  `git ls-remote origin refs/heads/master` after pushing.
- The local `master` ref is **stale** (`f942725`, "wsl-migration WIP snapshot") — never use it
  as ground truth. `origin/master` is truth.
- 29 merged remote branches were deleted on 2026-10-20. Kept: the above + `issue-108-*`
  (landed, verify), one opencode worktree (`bs1-issue96`, detached `9ba36f6`) — leave it alone.

## Ecosystem ops

**fin-sdk 3.2.0** — version policy: MAJOR tracks the contract (3) → 3.x.0; module
additions/changes bump MINOR; provenance re-stamps bump PATCH. `vendor.sh <Fin checkout>`
regenerates `lib/` + `MANIFEST.md` (sha256 per module, pinned cite). CI gate
`provenance.yml` checks out Fin at the manifest's cite and asserts `git diff --exit-code lib
MANIFEST.md` — the reason the current bundle does not drift, and the step that will force the
3.3.0 re-vendor.

**Linf v3.0.0** — architecture non-negotiable: finc is the source of truth (`finc <path>
--diagnostics=json --color=never`, capture merges `2>&1` because finc's stdout is
contract-silent); finc's own summary `exitCode` is the authoritative exit (popen waitstatus is
raw). Exit mirror 0/1/2/3. Build:
`finc src/linf.fin -o linf --fin-libs <fin-sdk>/lib`; check with `tests/run.sh` (28 asserts).
Lanes: contract / gcc (`$gcc` matcher, Neovim errorformat) / actions / sarif (terminal artifact,
five rules declared). `INTEGRATION.md` has copy-paste harnesses.

**CI workflows**: Fin `ci.yml` (six required matrix rows, `fail-fast: false`; perf + sanitizers
optional), `release.yml` (tag-fired full matrix + Alpine musl; historically green,
run 36516630186), `deploy-pages.yml` (single channel), CodeQL. fin-sdk `provenance.yml`; Linf
`self-check.yml` (builds finc from source at the pinned LLVM major and runs the 28 asserts).

## Landmines (each cost real time)

- **Never stage broadly.** `CMakeUserPresets.json` is tracked-but-never-committed and sits
  modified-unstaged; never `git add -A`, never `git commit -a`, never amend. Stage explicit
  paths; before committing check `git diff --cached --name-only`.
- **gh bodies:** `-f body=@file` passes the literal `@file` (it clobbered a comment once). Use
  `--body-file`, `--notes-file`, or `jq -n --rawfile b file '{body:$b}' | gh api … --input -`.
- **MSVC vs raw literals:** classic MSVC preprocessor cannot stringize raw literals containing
  `\"` (C2017/C3688) — hoist to named `const std::string` (pattern in `tests/test_stdlib.cpp`).
- **macOS linker reality:** public-symbol availability is stricter than the man pages suggest —
  `getdirentries64` does not link. Prove any new libc extern against the real platform before
  claiming a lane fix.
- **LLP64:** C `unsigned long`/`long` are 32-bit on Windows, but Fin `ulong`/`long`
  are 64-bit. Use `%llu`/`%lld` for Fin values and match each extern to the actual C ABI.
- **Weak symbols can be preempted** by libc on darwin (`warn`) — publish library symbols under a
  namespaced LLVM name (`fin_warn` pattern) when libc owns the spelling.
- **Stage-vs-ref agreement is runtime-output identity**, stdout **and** stderr byte-exact. A
  construct the backend cannot lower is a **named refusal**, never a silent drop.
- **Macro-invocation args are invisible to the stage checker** (unused-var warnings tolerated);
  `ast_macro_invocation()==31`; `LLVMValueArgs` 64 slots; LLVM block args only `b0`/`b1`.
- **Regenerate measurements**, don't quote: sample census (the chain comment in
  `tests/test_expectations.cpp`), test count, module count.
- **`--name=value` is not a recognized spelling in `cli.fin`'s grammar** (two tokens
  `--format gcc`); finc's own `--diagnostics=json` is a separate native parser. Do not conflate.
- **Scratch space:** `/tmp/opencode` root content is shared/ephemeral (a prior uid note said
  unwritable); use `~/fin-tmp/` for durable scratch, subdirectories otherwise. Never write
  scratch into the Fin repo.
- **Docs gate:** `check_docs.py` verifies fenced Fin examples; plain `fin` means a complete
  runnable program, exceptions name themselves (`fin fragment`, `fin check`, `fin error`,
  `fin build-error`); `output` fences are exact stdout. `docs/adr/README.md` is exempt from the
  ADR naming gate; ADR numbering is 0001–0059 with 0024 reserved.

## Subagent operating guidance

- **Disjoint file scopes.** Parallel agents must not share files; a shared clone means
  uncommitted work in another agent's files can be swept accidentally — stage explicit paths,
  never `git add -A`, and check `git diff --cached --name-only` before every commit.
- **No commits from a delegated agent** unless the brief says so; deliver diffs + a verification
  report, and let the coordinator land them.
- **Verification is part of the deliverable.** Each agent must run its own gate (local ctest,
  `check_docs.py`, the relevant workflow) and report the exact final line, not a summary claim.
- **Watch CI in the background** (`gh run watch <id>`, no polling loops); while it runs, do work
  with disjoint files or stop. Never tag or release on a prediction.
- **Report honestly.** A named blocker with an assertion line and a run id is worth more than a
  green claim; "loud refusal" beats a silent pass.

## Command cheat sheet

```bash
# truth
git ls-remote origin refs/heads/master
gh run list -R M1778/Fin --limit 5
gh run view <id> -R M1778/Fin --json jobs -q '.jobs[] | "\(.name)=\(.conclusion)"'
gh release list -R M1778/Fin

# per-lane failure forensics
gh run view --job <job-id> -R M1778/Fin --log > ~/fin-tmp/lane.log
grep -aE '\(Failed\)|The following tests FAILED|undefined symbol|error C[0-9]+' ~/fin-tmp/lane.log

# publish (explicit refspec — see Branches)
git push origin issue-101-asan-type-leaks:master

# Release v1.0.0 only after G2 passes and publishing is explicitly authorized.
# Preserve the historical v0.4.0 tag.
gh run watch "$(gh run list -R M1778/Fin --workflow=release.yml --limit 1 --json databaseId -q '.[0].databaseId')" -R M1778/Fin
```

## Definition of done for this handoff

The receiving agent has finished the release gate when: all six required lanes are green on
`master`; `v1.0.0` exists and `release.yml` completed green for that revision; fin-sdk is re-vendored to the new
`master` with provenance green and Linf re-pins against it; `check_docs.py` passes; this file's
"Verified state" and "GATE" sections are rewritten to the new reality (with fresh
measurements); and the owner decisions above are either recorded or explicitly listed as
still-open. If a lane cannot go green, the blocker must be named, evidenced, and owned — never
worked around silently.
