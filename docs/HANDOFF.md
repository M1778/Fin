# Handoff — Fin compiler, v1 production readiness

Updated **2026-11-04** after the focused repairs were committed through
**`17239b5`**, followed by this handoff update. The user approved commit/push
on that date; the reviewed changes are being published to candidate branch
`issue-101-asan-type-leaks` for native CI. Remote readback before publication
confirms `master` remains at `1f7afd9`. No merge, branch deletion, tag, release,
or deployment is authorized by this handoff.

This file is the **single entry point** for the next agent — self-contained by design, including
for a fresh subagent with no conversation history. It states what is done, what blocks a
production release, the traps that cost real time, and the order of work. Measurements below
belong to their dated checkpoints; **re-measure before quoting**, and re-read `AGENTS.md` (canonical)
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

## Current checkpoint — 2026-11-04

Candidate **`15acb4d`** contains the October maintenance recorded below; those
changes are committed and pushed to the candidate branch, not to `master`.
CI run [40254354645](https://github.com/M1778/Fin/actions/runs/40254354645) tested
that exact revision: both Linux lanes, build.sh, performance and sanitizers passed.
Each macOS lane failed one path regression; each Windows lane failed one
clock-fixture link regression. Native-stage parameters remain explicitly skipped
on non-Linux runners; reference-only green is not self-hosted native proof.

Focused repairs committed for candidate publication:

- `2a86eb1` — isolate owned path results from libc symbols.
- `ddb0923` — isolate deterministic clock fixture symbols.
- `17239b5` — repair the scheduled contract version parser.

- `path::std`'s `basename` and `dirname` now publish `fin_path_basename` and
  `fin_path_dirname`. Their Fin spellings and caller-owned results are unchanged.
  A strong native-symbol fixture reproduced wrong binding on Linux before the
  fix; the repaired regression passes at `-O0` and `-O2`. This addresses the
  macOS path test's libc-preemption mechanism without changing path parsing.
- The deterministic deadline test now remaps only the `llvm_name` clock externs
  in a temporary copy of the real `time.fin` module. Its C helpers have private
  names and the host clock remains callable. This avoids duplicate
  `QueryPerformanceCounter` definitions from the Windows fixture and
  `kernel32.lib`, retaining all four offset cases at both optimization levels.
- Scheduled contract check run
  [41446321204](https://github.com/M1778/Fin/actions/runs/41446321204) failed in
  `sed`, before any contract comparison: it referred to capture group `\1`
  without defining one. The workflow parser is corrected and its new check
  exercises the actual expression against eight valid/invalid version spellings.

Local focused gate: **10/10 tests passed**, including public path behavior on
Reference/Stage2/Stage3. Full Debug suite: **3,159 passed, 60 explicitly skipped,
zero failures** (3,219 checks, 587.98 seconds). No new skips were added by these
repairs. The rebuilt Release focused gate passed **18/18**, including path,
runtime-portability and contract checks. The focused compiler ASan/UBSan gate
passed **16 tests, two missing-stage skips, zero failures**, with leak detection
and UBSan halt-on-error enabled. This instruments the compiler/test process,
not arbitrary memory accesses in its emitted programs.

A fresh installed Release tree passed **75 doc examples, six fragment skips**.
The public path program passed `-O0`/`-O2` from a foreign directory with
`FIN_LIBS` unset, stdout exact and stderr empty. Object inspection confirmed
`fin_path_basename`/`fin_path_dirname` definitions and no bare native path
symbols. Existing Stage2/Stage3 binaries remain byte-identical at SHA-256
`59467f91730305bf2db5535675eafa07ece0109409b520eae18b383c5aaa5998`;
no new bootstrap was needed for these library/test/workflow-only repairs.

The eight workflow-parser cases, site check, architecture/installer preflight
and `git diff --check` pass. Independent read-only review found no actionable
issues in the patch. Commit/push authorization was granted on 2026-11-04.

**Native CI completed:** run
[41770346027](https://github.com/M1778/Fin/actions/runs/41770346027) tested exact
candidate **`3e801a02c31e7d0f17c8243465b2727d2b1f3caf`** and passed all six native
lanes, build.sh, performance and ASan/UBSan. This supplies native reference
proof for the path and clock repairs; non-Linux self-hosted stages remain
outside the bootstrap coverage described above.

**Contract integration remains blocked:** run
[41770345868](https://github.com/M1778/Fin/actions/runs/41770345868) tested the
same revision. The parser check, compiler build and Fin-side corpus replay
passed, then version agreement correctly failed: finc speaks **contract 3**,
while the checked-out published `M1778/finn` implements **contract 2**. Its
test-suite step was not reached. Repair and verify the consumer's contract-3
behavior, not merely its constant; current `AGENTS.md` keeps the local finn
checkout read-only, so that work needs explicit scope approval.

Next gates: resolve consumer compatibility, rehearse release packaging on the
validated sources, and obtain separate merge/publication authorization.
`master` has not been updated and no `v1.0.0` tag or release was created by
this candidate publication.

## Historical verification checkpoints

The following results retain their dates and revisions. Descriptions of work as
"local", "uncommitted", or "pending" below describe that historical checkpoint,
not the current candidate's git status.

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

**Candidate CI follow-up (2026-10-24):** corrected commit `5360b05` is pushed only
to `issue-101-asan-type-leaks`. Run **40028977601** passed all six native lanes,
ASan/UBSan, build-script, and performance jobs. Native logs confirm target selectors,
imported selector execution, qsort and thread callback results, mutex/thread/channel
behavior, and joined cleanup passed on every platform. Both Windows lanes passed
all 11 logging/filesystem tests without skips. The preceding `cc7f166` run
40026847877 failed on a bare `ws2_32.lib` linker argument and unsupported
`#c_ifndef` in sync cleanup; `5360b05` corrects both with regressions.

**Current maintenance (local, not pushed):** issues #136 and #135 reproduce on
`5360b05`. `random::std::rand` now exports the C runtime function directly rather
than defining a colliding wrapper. BTreeMap ordering on a nonempty map without a
comparator now blames before calling null; its empty/first-insert behavior is
unchanged. Issue #138's inclusive int32-span overflow is repaired by wide span
arithmetic, three-draw 32-bit sampling and rejection of the incomplete bucket. A
deterministic 15-bit C-rand shim verifies endpoints and rejection, not just bounds.

The random range repair uncovered a stage `cast<long>(negative int)` bug: it
zero-extended, producing 4294967295 for -1. The explicit cast now preserves source
signedness, with a cross-stage regression for signed/unsigned/bool values. Scratch
Stage2/Stage3 converge (SHA-256
`62b8e413e315e37fe11e3bd7323363f21d6f3f6e88890438ebc7d5dc3f9250a3`).
Eight focused tests pass; 12 separate Reference/Stage2/Stage3 O0/O2 wide-range and
shim executions pass with allocator-sanitizer linkage. Focused compiler sanitizer
tests passed six, with two missing-stage skips; optimized reference tests passed
five. Docs passed 74 examples. The full local suite passed **3,054 tests,
57 skipped, zero failures** (3,111 checks, 260.49 seconds). The native green
checkpoint above predates these uncommitted compiler/library changes. Issues
#140–#142 remain separate, unimplemented slices; #139 is covered below.

**Deadline maintenance (#139, local):** `deadline_after_ms` now checks offsets
against the remaining signed-long nanosecond range before multiplication/addition.
A deterministic near-limit clock stub verifies zero/1ms/2ms, one-past addition,
multiplication overflow and the existing negative-offset blame at `-O0`/`-O2`.
The reference Debug, Release and focused ASan/UBSan gates pass. Cross-stage testing
also exposed dynamic `[long]` arithmetic narrowing (#144) and implicit negative
int-to-long argument widening (#143's call path); both have failing-before public
regressions and local fixes. All **24 deterministic deadline executions** pass
across Reference/Stage2/Stage3 at `-O0`/`-O2`; nine cross-stage integer regressions
pass. The focused compiler sanitizer gate passed 12 tests with six missing-stage
skips. Scratch Stage2/Stage3 converge (SHA-256
`f6310759f2d57eeb6b9c602439d921f1011d70da90743a25bee919f597d13b5c`).
The full local gate passed **3,061 tests, 57 skipped, zero failures**
(3,118 checks, 267.99 seconds). Twelve additional real-clock overflow
executions passed across all stages at both optimization levels. Native CI at
`5360b05` does not cover these uncommitted changes.

**Base64 maintenance (#140, local):** decoding now rejects any '=' before its
zero/one/two-byte contiguous terminal suffix. Leading, discontinuous, excess and
nonterminal padding blame before decoded-output allocation. Null/empty inputs and
existing length/alphabet diagnostics remain; unused pad bits stay permissive.
Native byte-comparator regressions cover valid tails, high bytes, embedded NULs,
terminators and noncanonical pad-bit controls. Both reference tests pass, including
ASan/UBSan and optimized builds; **96 allocator-linked cross-stage executions**
pass at `-O0`/`-O2`. The full local gate passed **3,063 tests, 57 skipped,
zero failures** (3,120 checks, 277.28 seconds). Independent read-only testing
reports 348/348 byte-sensitive cases passed across all stages and both optimization
levels. These library changes remain uncommitted; no new native remote CI applies.
#141/#142 are covered in the later maintenance sections.

**Deque lifecycle maintenance (#141, local):** explicit `free_deque`,
`free_queue`, and `free_stack` release backing buffers, reset to empty and allow
lazy reuse. `clear()` retains capacity. Methods are idempotent for one owner;
copies still share handles, pointer/string payloads remain caller-owned, and no
implicit destructor is introduced. Nine cross-stage regressions first failed for
the absent API and now pass at `-O0`/`-O2` with Linux allocator checks. The pointer
payload cases also exposed stage receiver lookup stripping an '&' inside
`Deque<&int>`/`Stack<&int>`; it now uses the existing outer-marker helper. This
returning-method path is independently tracked as #145, with a regression reading
`Deque<&int>.pop_front()` into a pointer binding.
Scratch Stage2/Stage3 converge (SHA-256
`9673f7e82858ee4c0e6acd95eba0b0a393590c5c69afb069309ec4881186c451`).
The expanded local full gate passed **3,072 tests, 57 skipped, zero failures**
(3,129 checks, 275.98 seconds). Eighteen separate allocator-linked lifecycle
executions pass; independent read-only verification reports 24/24 allocation-balanced
cases passed. Focused compiler sanitizer tests passed three with six missing-stage
skips; optimized reference/stage tests passed nine. Docs passed 75 examples with six
fragments skipped. Repairs remain uncommitted and not covered by new native CI.
Windows path issue #142 is covered below.

**Windows path maintenance (#142, local):** the approved stdlib-only repair
recognizes both separators, drive-relative/root-relative/drive-root prefixes and
complete UNC shares. Fully absolute replacements, same/different-drive joins,
root-preserving basename/dirname, and prefix-aware lexical normalization are
covered; inserted/normalized separators remain '/' to preserve existing output.
Device/verbatim namespaces and incomplete nonempty UNC roots blame by name.
DBCS forward boundary scanning protects a trailing 0x5c byte. Five focused tests
pass, including reference/stage POSIX controls and supplemental Windows prefix,
DBCS-shim and named-refusal cases. Six allocator-linked Windows-selected matrices
pass across all stages at `-O0`/`-O2`. Focused sanitizer tests passed three with two
missing-stage skips, optimized current reference/stage tests passed five. Frozen
candidate source: `build/release-check/path-work/path-candidate-58a658e.fin`,
SHA-256 `58a658e18c16116075ce25cc4fa9894b9ca5971fba0fbd57f539d2d48fb07bd3`.
The final same-source local full gate passed **3,077 tests, 57 skipped, zero
failures** (3,134 checks, 278.59 seconds), including the final native-byte DBCS
fixtures.
Independent exact-final candidate verification reports **410 native Windows** and
**1,350 POSIX** byte/terminator/refusal cases passed, with hashes stable before/after.
Six additional allocator-linked DBCS-byte fixture runs pass across all local
stages. Independent native Windows verification used ACP1252 and observed
`IsDBCSLeadByteEx(0, 0x83) == false`; that native DBCS fixture was explicitly
skipped without changing the host code page. These supplemental tests alone
are not native Windows proof; repairs remain uncommitted and are not covered by a
new six-platform CI run. The independent native report uses the previously verified
Windows compiler artifact with the frozen candidate library, not a compiler rebuild.

**Negated-keyword macro maintenance (#146, local):** the stage text expander
now consults `token::std::lookup_keyword` before interpreting identifier-plus-bang
as an invocation. Reserved keywords such as `blame` and `return` followed by unary
parenthesized negation no longer become undefined `blame!`/`return!` macros.
Genuine identifier/dotted macros, '!=' handling, strings/comments, and undefined
identifier macro diagnostics keep their existing behavior. Nine public cross-stage
regressions first reproduced the defect and now pass at `-O0`/`-O2`.
Scratch Stage2/Stage3 converge (SHA-256
`715ea9a9fcb9644899a40060493356c3d04b887946271a8fd012b1ccf017a49c`).
The expanded local full gate passed **3,086 tests, 57 skipped, zero failures**
(3,143 checks, 283.70 seconds). Eighteen extra compile/runtime boundary checks
passed, including allocator-linked valid programs and unchanged unknown-macro
refusals. Optimized reference/stage tests passed nine; focused compiler sanitizer
tests passed three with six missing-stage skips. Independent scoped boundary
verification passed **192/192**; its expanded matrix passed **196/204**, retaining
eight pre-existing comment-trivia failures subsequently tracked as #147. Changes
remain uncommitted and not covered by a new native six-platform CI run.

**Comment-trivia macro maintenance (#147, local, 2026-10-25):** stage invocation
lookahead now skips whitespace, line comments, and nested block comments at both
bang boundaries without mutating the scanner cursor. One nested-comment end
helper also protects ordinary comment copying and declaration/argument scans.
An expanded red test caught macro-looking text after an inner `*/`; it now stays
inside its outer comment. Parser `expect` preserves scanner error tokens rather
than replacing an unterminated-comment diagnostic with `expected ';'`.
All **24 focused parameterized tests** pass (Reference/Stage2/Stage3, both
optimization levels inside each test), covering keyword negation, genuine/unknown
calls, dotted imports, bracket/braced arguments, nested comments, quoted/comment
text, following diagnostic lines, and unterminated trivia at both boundaries.
Stage2/Stage3 converge at SHA-256
`4dee1965a176a1f1f8b9452389171b08ab25d88d6aa5edea551a55c0bba79355`;
stable scratch paths are `build/ownership-work/trivia-final-stage2` and
`build/ownership-work/trivia-final-stage3`. Driver source SHA-256:
`58a6ec764293ae8904cacf456b789fb313e27467051a3a7104b4bb5d5ba8dd7e`;
parser source SHA-256:
`964f39fe425d713bd061afbc14491637840550e664470e1bf59804a358e086e3`.
The full local gate passed **3,101 tests, 57 skipped, zero failures** (3,158
checks, 540.90 seconds). Documentation: **75 examples passed, six explicit
fragments skipped**; formatting and site checks passed. No new native remote CI
or publication is implied. #148 prototype tests were added after this historical
full gate; their later results are recorded separately below.

**String-key prototype maintenance (#148, local, 2026-10-26):** lookup, store,
`delete &p[key]`, and `p.rm(key)` now share a per-entry key-kind comparison.
String keys compare contents, including separately allocated equal strings;
integer and opaque words never enter the string-byte path. Null keys avoid byte
reads, and `memcmp` is bounded by the shorter string. Equal-key stores update
rather than append. Growth appends key tags; erasure shifts keys, values, and tags
together and updates both lengths. Unsupported probe/store/erase key forms
explicitly refuse instead of emitting invalid operands or guessing slot zero.

The first homogeneous-tag candidate failed three full-suite agreement checks
on the normative mixed-key `prototype_test.fin`. Per-entry tags repair that
regression without changing its expectation or adding an allowlist. Follow-up red
checks caught invalid array deallocation through key-half aliases, repeated
literal-receiver evaluation during cleanup, untracked call/cast/return escapes,
and key annotations overwritten by a differently typed shadow binding.
Concrete key-half initializer aliases now retain their header provenance and
release the tag buffer with the key allocation; incompatible/erased projection
writes and direct key-buffer call/cast/return escapes explicitly refuse.

The current candidate passes **60 focused checks, three explicit reference skips,
zero failures** (63 checks, including all three `prototype_test` agreement checks;
prototype programs test both `-O0`/`-O2`). Stage2/Stage3 converge at SHA-256
`59467f91730305bf2db5535675eafa07ece0109409b520eae18b383c5aaa5998`;
stable scratch paths are `build/ownership-work/prototype-scoped-stage2` and
`build/ownership-work/prototype-scoped-stage3`. Codegen source SHA-256:
`ba154fa8453af966c176ce6bb10568c7321c9a3a474d080b7915fbac616c7726`.
The earlier `prototype-verified` full run was stopped after the scoped-annotation
regression was discovered; it is not a completed gate. The corrected candidate's
full local gate passed **3,158 tests, 60 skipped, zero failures** (3,218 checks,
544.18 seconds). The skips include the three explicit reference boundaries above;
no normative sample expectation or stage allowlist was changed.

Allocator-linked checks pass **72 compile/run combinations**. Generated Fin
loads/stores are not sanitizer-instrumented, and leak detection was disabled for
the existing prototype-header ownership limitation. Four additional allocation
interposition audits confirm key/value/tag buffers are released through direct
and aliased cleanup at both optimization levels, while existing header allocations
remain retained. Focused reference-compiler ASan/UBSan checks pass **17 tests,
43 skipped, zero failures**: 40 missing-stage parameters and three explicit
reference skips. Documentation passes **75 examples, six explicit fragment skips**;
site and formatting checks pass. The user authorized commit/push and CI checking
on 2026-10-26. These maintenance changes are being published on the existing
candidate branch, with a fresh `ci.yml` dispatch after push; its native results are
pending and the older green run is not proof for this revision. Release/Pages
workflows are not dispatched. This slice does not establish general prototype key/value
support, half rebinding/aggregate escape safety, borrow checking, or whole-header
ownership; `get`, `contains`, and `remove` stage support remain outside the claim.

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

**Earlier production-candidate work (committed by `5360b05`):**

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
- Local `master` was fast-forwarded from `f942725` to `origin/master` @ `1f7afd9`
  on 2026-10-26, without changing the worktree. Fetch and compare remote refs before
  choosing a base; the candidate branch remains separate until a merge is authorized.
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
- **Clean up your scratch when the task ends.** `~/fin-tmp/` reached 8.9 GB in six weeks
  because every agent left its build trees behind and nothing pruned them. Before reporting
  done: delete the directories you created (`rm -rf ~/fin-tmp/<yours>`), and leave anything
  still in use by a concurrent agent alone. A Conan toolchain directory under scratch is
  referenced by an absolute path from `build-asan/CMakeCache.txt`, so check `grep -rl <dir>
  ~/Fin/build*/CMakeCache.txt` before deleting it.
- **Build directories are disposable.** `build/` (Debug, ~1.4 GB), `build-asan`,
  `build-stage`, `build-explain`, `build-pic`, and `build/release-check/` can all be
  regenerated; delete them when a workstream ends rather than accumulating one per agent.
  `~/finn/target` (Rust, ~4.5 GB) is the same kind of cache.
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

---

# 2026-11-21 — `v1.0.1` cut, and ten ecosystem repositories hardened

Everything below supersedes the "GATE" section above for the 1.0.1 release. The sections
above are kept because they carry held rulings, ADR bookings and historical checkpoints that
nothing here replaces.

## What shipped

| Artifact | State |
| --- | --- |
| `finc 1.0.1` (contract 3) | Tagged **signed**, release workflow green |
| `finc 1.0.0` | Published, **prerelease**, kept as a record — do not repair it |
| `finn 1.0.1` | Two user-facing defects fixed; `--source` download route added |
| Eight libraries | All merged, all green on Linux/macOS/Windows |
| `fin-vscode` | Real `.vsix`, 56 tests, three-OS CI |
| `fin-lsp` | 86 tests, verified over a live stdio session |

Local suite **3,278 tests, 100%**; nine CI jobs green on `a580c63`.

## The four compiler defects 1.0.1 fixes

1. **A null `[char]` handle segfaulted before `main`.** `let a <[char]> = null;` emitted
   `strlen(null)`. `Soundness_Codegen.ANullStringToCharArrayDoesNotCallStrlen` pins it.
   The first fix did NOT work: a runtime null check still crashed because LLVM hoisted the
   `strlen` above the comparison guarding it. The call must be emitted inside a block the
   test selects. If you touch this, keep it there.
2. **`-lLLVM-22` on every link.** A reference-compiled object references no LLVM symbol, so
   the flag broke `finc -o app` on any machine without the LLVM *development* package.
   `-Wl,--as-needed` never helped: it decides after the linker has already found the library.
3. **An imported `const` was never declared.** `declareGlobals` walked only the root, so 168
   `pub const` declarations in `lib/std` were unusable outside their own file.
4. **`finfmt` was never installed.** No earlier release contained the formatter at all.

## New traps, on top of the ones above

- **Pushing to `master` cancels the running CI** (`cancel-in-progress: true`). I cancelled my
  own gate three times. Land everything, push once, wait.
- **The release tag must be signed.** `release.yml` refuses an unsigned tag; `v1.0.1`'s first
  attempt was refused. SSH signing works: `git config gpg.format ssh`,
  `git config user.signingkey ~/.ssh/id_ed25519.pub`, then `git tag -s`. `v1.0.0` is
  annotated but unsigned because it predates the rule.
- **The contract's `column` is a UTF-8 byte offset**, not a character offset, and the JSONL
  goes to **stderr**. Both were undocumented until the editor work; both are now pinned.
- **A platform-guarded branch is code only one platform parses.** `tests/tools/check_foreign_branches.py`
  flips the guards so any host can type-check the other branch, and restores the file.
- **`::add-path::` is retired**; printing it fails the step. Use `$GITHUB_PATH`.
- **macOS x86_64 runners can die silently** — in-progress with no active step and no logs.
  Re-dispatch; both times it passed.
- **A stale `fin_tests` binary produces phantom failures.** Rebuild before believing a
  failure that names a test you cannot find in the source.

## Known-broken, deliberately shipped

Full reproductions in `docs/known-issues.md`. Summary: macOS/glibc artifacts need LLVM at run
time (documented, gated by `check_runtime_deps.py`, not made self-contained); a `&char`
parameter cannot be indexed with a literal; hex literals do not exist; a Fin `string` cannot
express an embedded NUL; the Windows CRT's `"ab"` is not an atomic append; a 64 KiB stack
buffer plus a spawned thread aborts on Windows; the stage `parse_float` defect; the fuzzer
does not build under Clang (#159).

## Ecosystem and editor tooling

`docs/ECOSYSTEM-HARDENING.md` records the per-repository work. The two patterns worth keeping:
CI installs a pinned release (`FINC_VERSION` + `install_finc.py` + `install_finc_runtime.py`)
rather than building the compiler, and every test was proven load-bearing by mutation — which
is how `fin-validate`'s untested `max_len` and fin-ssg's unparsed Windows branch were found.

`fin-vscode` and `fin-lsp` build on `docs/stdlib-index.json` (49 modules, 543 members,
`--check`ed in CI). Neither is in a package registry, and the site says so.

## Proven, in the way that counts

`v1.0.1` is published: 41 assets, signed tag, not a prerelease. What follows was verified by
downloading the assets and running them, because 1.0.0 was green and broken for exactly the
opposite reason.

- musl x86_64: `finc 1.0.1 (contract 3)`, static-pie, compiles a program importing both
  `strings::std` and `parse::std`, and ships `bin/finfmt` -- which no earlier release had.
- gnu x86_64: **the artifact that segfaulted in 1.0.0 answers `--version` and compiles.**
- `.deb`: `Depends: libllvm22`.
- darwin aarch64: the load command is the Homebrew path `INSTALL.md` documents. Inspected, not
  executed; this host cannot run Mach-O.
- `finn download` with no arguments installs 1.0.1, and `finn init` → `finn build` → run works.
  Its release-smoke CI job is green on Linux, macOS and Windows against the published release.

## Not done

macOS/glibc artifacts are not self-contained; `fields_of` is a signature-only marker (ADR
0057); struct `==` stays declared-only (ADR 0036); self-hosted stage agreement is Linux-only;
neither editor tool is published; the `macos-x86_64` runner flake has no fix.

---

# 2026-11-22 — post-release: five more defects, and the fuzzer finally builds

The day after 1.0.1, working through the open issue list. Every item below was reproduced
before it was touched, and every fix has a test that was red first.

## The fuzzer builds, and that closed a real portability hole

`Value::fields` was `std::vector<std::pair<std::string, Value>>` — a vector whose element type
names the enclosing struct. libstdc++ tolerates the incomplete type there; Clang does not, so
three translation units failed under Clang, so the compiler did not build under Clang at all,
so `FIN_FUZZ=ON` (which needs Clang for libFuzzer) could never succeed. The claim "we fuzz the
parser" was false.

It is a `std::map<std::string, Value>` now. That is portable, and better on its own terms:
field lookup was a linear scan and the constructor hand-rolled an O(n²) last-wins dedupe that
`fields[name] = ...` does in one step.

Verified: the Clang build links `fuzz/fin_parse_fuzz` (11 MB, libFuzzer + ASan + UBSan), and
`./fin_parse_fuzz -max_total_time=30` ran **21,679 iterations with no crash**. A nightly job
(`.github/workflows/fuzz.yml`) now keeps that true, with a persistent corpus so coverage
accumulates.

## String escapes were silently dropping their backslash

`"caf\xc3\xa9"` produced `cafxc3xa9` — nine bytes instead of five — and `"caf\u00e9"` produced
`cafu00e9`. Both decoders (codegen and the comptime interpreter) had a `default:` arm that kept
the character and dropped the backslash, which is a silent wrong answer of the kind the backend
invariant forbids.

There are now **one** decoder, `src/utils/StringEscapes.{hpp,cpp}`, used by both, supporting
`\xNN` (a raw byte) and `\uNNNN` / `\u{NNNNNN}` (a code point, encoded as UTF-8). Duplicated
tables drift, and a literal that meant one thing at run time and another under `comptime` was
the drift this removed.

Verified: `\xc3\xa9` is 5 bytes, `\u00e9` and `\u{e9}` both 5, `\u{1F600}` is 4 (the emoji case
that catches a two-byte-only encoder). A malformed `\x9` or an unpaired surrogate keeps its
character rather than emitting half a character.

## `process::std` reported the wait status as the exit code

`exec_command("sh -c 'exit 7'").exit_code` was **1792**, not 7. `pclose` and `system` answer a
wait status on POSIX and both were returned raw, so every caller comparing `== 1` was reading
256 and `is_success` was the only predicate that happened to work. Present since the standard
library's second day; nothing tested it.

Decoded now: `(status >> 8) & 255`, with a signalled child answering 128 + signal — the shell
convention. Windows returns the code directly and is passed through by `#c_ifdef`.

## Four guide pages said features were missing that work

Each was verified by compiling and running before the page was rewritten:

| Claim | Reality |
| --- | --- |
| "multiple constructors are refused" | arity-distinct ones dispatch; same-arity ambiguity is refused *by name* |
| "the compound-assign forms are not in the grammar" | five of six work; only `^=` is absent |
| "`match` is not implemented" | it is an expression, `=>` arms, `_` wildcard (ADR 0050) |
| "the backend cannot box or cast through `any`" | boxing, exact-type unbox, equality all work, and a mismatched unbox blames |

All four examples are compiled by the docs gate: 80 examples pass, up from 76.

## The collection JSON decoders accepted trailing junk

`json_decode_int("5 junk")` refused and `json_decode_int_array("[1,2] junk")` succeeded. The
module header claimed every decoder made a trailing-data check; three did not. Six call sites
fixed (two per decoder, including the empty-collection exits), pinned by
`Soundness_Serde.ACollectionDecoderRefusesTrailingData`.

## Also

The v1.0.1 release page carried 14 lines of packaging notes while a 247-line narrative sat in
the repository; it now carries the narrative plus the generated asset block.

## Three tests encoded something other than what they were named for

Worth recording as a pattern, because all three looked fine and all three allowed a defect to
stand or made a fix look like a regression:

1. `Soundness_RuntimePortability.ProcessCapturesOutputAndPreservesFailureStatus` asserted
   `run_command("exit 7") == 1792` on POSIX and `== 7` on Windows. The platform split was the
   tell: `exit 7` cannot mean two numbers. It was pinning the raw wait status as if it were the
   exit code.
2. `Soundness_Encoding.Base64RejectsMalformedPaddingStructure` asserts a non-zero exit and a
   `blame` message. That is arguably correct — `blame` is a documented raise (ADR 0053) — but
   it means the "does this decoder refuse untrusted input by value" question is answered by the
   test rather than by the language, and a change to that answer would have to rewrite it.
3. My own `Soundness_Process.TheExitCodeIsDecodedNotTheRawWaitStatus` passed locally and failed
   on every CI lane because the shell prepends `Killed` to the capture. Every value it asserted
   was right; the test was asserting dash's stderr handling.

The checks that catch this class: does the assertion name a property or a number, and does it
pass on more than one platform? A test that passes only on the machine it was written on is
usually testing the machine.

## Compile-time performance work: measured, decided, merged (2026-11-23)

The request was a parser/lexer rewrite, possibly in Fin. **Measured, the parser and lexer are
0.08 ms of a 45 ms compile — 0.2%**, so the rewrite was rejected on evidence and the work went
to where the time is. Full record: `docs/PERF-DECISIONS.md`.

- **Debug vs Release is 3.6×** on a ten-module check (159.9 → 44.8 ms). Every perf number must
  name its build type; `tests/tools/bench_compile.py` now refuses to measure a Debug binary.
- **71% of a Release compile is semantic analysis of the ten stdlib modules**, parse 17%,
  preprocess 6%. A `json::std` import pulls ten modules, every compile, unchanged between builds.
- **7 ms of every invocation is libLLVM mapping**, used or not — 49% of a trivial check. A
  frontend-only binary would cut a check by 21% and a build by 5%, but it weakens `finn`'s
  install verification (`healthcheck` passes a backend-less binary; only the build refuses).
  Not taken; recorded with numbers.
- **The type-graph registry costs ~17%** — a global hash every `Type` joins and leaves, existing
  to break leak islands (issue #101). The fix is arena allocation and needs a ruling.
- **64 ms of a build is the external link**, `cc` at 77 ms against `ld` at 13 ms. Not taken in
  this pass; it needs crt paths derived from `cc` rather than hardcoded.

Shipped: the benchmark harness (`tests/tools/bench_compile.py`, 53 scenarios, three-outcome
gate), a baseline, and the decision record. Its first compare reported everything 10-24% slower
because the test suite was running concurrently — it answered "inconclusive" rather than
guessing, which is the behaviour that makes a benchmark trustworthy.

Merged to `master` with all nine CI lanes green, and the benchmark gate inconclusive-to-passing
on the merged tree.

## Verified at the end of 2026-11-22

- Local suite: **3,282 tests, 100%**.
- CI on `985d218`: **all nine lanes green** — linux x86_64/arm64, macOS x86_64/arm64, Windows
  x86_64/arm64, sanitizers, `build.sh`, perf.
- **The nightly fuzz job ran green for the first time**: Clang built the harness, the corpus
  restored, 120 seconds of fuzzing, no crash.
- Thirteen issues closed with reproductions (#159, #149, #140, #135, #134, #133, #132, #131,
  #129, #126, #123, #122, #120). 23 remain open and are real.

Two things to know before trusting a lane's green:

  * the index gate (`gen_stdlib_index.py --check`) fails when a `lib/std` module changes and
    the projection is not regenerated. It caught me twice. Run it after any stdlib edit;
  * the `macos-15-intel` lane stalls without a log roughly one run in three. Cancel and
    re-dispatch — the tag and the commit do not change, so the verdict does not either.
    `docs/known-issues.md` has the `BlobNotFound` signature that distinguishes it from a slow
    build.

## 2026-11-22, second pass: three codegen gaps closed

Thirteen issues closed in total today. The three below are features rather than fixes, and each
was refused at codegen while its opposite direction worked -- which is what made them read as
defects:

- **`cast<int>(3.9)`** truncated toward zero. int-to-float worked and float-to-int refused. The
  care was in the range: `fptosi` is undefined for out-of-range values and NaN, so the lowering
  clamps on the float side where the bounds are exactly representable, and NaN answers 0. A
  caller converting a computed ratio no longer has an undefined answer.
- **`s.length` on a string** is the byte count, agreeing with `strings::len` and answering 0 for
  null. `lib/std/strings.fin` had documented the refusal as a fact of life and told readers to
  use `len`; that comment is now corrected. A `[T]`'s `.length` is untouched -- it has its own
  length word and this arm must not shadow it.
- **A `ulong` literal's upper half** works. `std::stoll` threw above int64 max, so a `ulong`
  could not spell ULONG_MAX. Parsed unsigned when it does not fit signed, which the declared
  type makes the right reading rather than a guess.

Also confirmed already fixed and closed with evidence: #135 (a BTreeMap with no comparator
blames by name instead of segfaulting) and #138 (`random_int` over the full int32 range).

### The pattern this pass produced

A test whose **premise is a gap** becomes wrong when the gap closes.
`AGenericFunctionsBodyIsCheckedAtEachBinding` proved "a template body is checked per binding" by
relying on `cast<int>(float)` refusing. Once that cast worked, the test failed for the best
possible reason. The fix was to move the witness to `cast<int>(a struct)`, which still refuses --
never to restore the gap so the test keeps passing.

Two more of today's tests failed on first CI run for a variation of the same reason: they
asserted something the platform does not promise (the raw wait status; a SIGKILL number Windows
does not have). A test is a claim about a contract, and the contract has to be the one that
holds on the machine it runs on.

## Measured, and deliberately not shipped: the type-graph registry costs 17% of a compile

`perf` on a Release `finc` compiling a `json::std` import puts **16.8% of the run** in the
type-graph registry: `typeGraphRegister` on every `Type` construction, `typeGraphUnregister` on
every destruction, plus the hash churn (`_M_emplace_uniq`, `_M_insert_unique_node`,
`_Hash_bytes`) and part of `PrimitiveType::~PrimitiveType`.

The registry exists for issue #101: the type graph is mutually recursive (a struct owns its
methods' signatures, which name the struct back), so `shared_ptr` alone leaves reference islands
that no ordinary destructor reclaims — 373,648 bytes leaked per `finc -c`. The registry breaks
the islands at exit by walking every surviving `Type` and dropping its type-valued edges.

There is no ADR for this. The header in `src/types/TypeSweep.hpp` is the whole record.

The correct fix is not "remove the registry" — that brings the leak back and the leak check
depends on it. It is to **arena-allocate types per compile**: an arena dies as a block at exit,
so there are no per-node registrations, no per-node destructions, no islands, and no sweep. The
obstacle is that types are `shared_ptr`-managed everywhere and hand out raw pointers that
outlive the analyzer, so the change is wide and needs a ruling.

Perf work on the analyzer should start here, not in the visitor: this 17% is structural, and the
visitor's 23.7 ms sits on top of it.

## Not settled, and deliberately

`base64_decode` **blames** on malformed input rather than refusing with a value. That aborts a
caller parsing untrusted bytes, which is worth a ruling — but `blame` is a documented raise
(ADR 0053), the existing test asserts the abort, and the function returns a non-nullable
`string`. Changing it is a signature decision, not a bug fix. Issue #140 is closed with the
question recorded rather than answered unilaterally.
