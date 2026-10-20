# Handoff — Fin compiler, v1 production readiness

Updated **2026-10-20** at `master` @ **`d5d25b7`** (+ this docs commit). Worktree branch
`issue-101-asan-type-leaks` is the trunk line; `origin/master` is the truth (see Branches).

This file is the **single entry point** for the next agent — self-contained by design, including
for a fresh subagent with no conversation history. It states what is done, what blocks a
production release, the traps that cost real time, and the order of work. Every number here was
measured on the date above; **re-measure before quoting**, and re-read `AGENTS.md` (canonical)
when anything here disagrees.

## Mission — what "production-ready v1" means

Not "all features". A defensible first stable release:

1. **Green required CI lanes** on `master`: linux-x86_64, linux-arm64, macos-x86_64, macos-arm64,
   windows-x86_64, windows-arm64 (linux lanes only are green today — see Gate).
2. **`v0.4.0` tagged and `release.yml` green** (six-platform + Alpine musl matrix; the tag is the
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

## Verified state (2026-10-20)

**Compiler**: `finc 0.4.0 (contract 3)`; `kFincContractVersion = 3`
(`src/driver/Version.hpp:38`); 49 modules in `lib/std/`; 75 samples; sample census chain in
`tests/test_expectations.cpp`.

**Local gates (Linux)**: full `ctest` **2931/2931**; `check_docs.py` **74 examples passed,
5 explicitly marked fragments skipped**; docs site gate `check_site.py` green (20 module anchors,
37 search entries).

**Shipped this cycle** (all on `master`):

| Area | Commit | Ruling |
| --- | --- | --- |
| reflect intrinsics | `47ceb96` | ADR 0057 (`fields_of`/`FieldInfo` still blocked) |
| `derive_hash` — FNV-1a 64 over field VALUES, declaration order | `300458f` | ADR 0058 (`derive_eq`/`derive_encode` gated on R3) |
| `enum_from!` builtin macro | `cdc9331` | ADR 0059 |
| docs sweep: guides/contract refresh | `4dfb4c6` | 74/74 examples verified |
| docs sweep: ADR addenda + index, checker carve-out | `0b69049` | dated addenda; `docs/adr/README.md` exempted from the NNNN gate |
| macOS libc `warn` preemption | `e901ae5` | `warn` published as `fin_warn` via `#[llvm_name]` |
| fs `walk_dir` dirent rework | `da6d1f6` | **BROKEN on macOS — see Gate B1** |
| MSVC raw-literal parse (C2017) | `0103886` | six literals hoisted in `tests/test_stdlib.cpp` |
| Windows arm64 `fork` (C3861) | `d5d25b7` | POSIX fence in `tests/test_sync_net.cpp` |

Closed issues: **#53** (reflect/derive/enum_from ledger, comment 6280766517), **#114**
(json/serde, `4a8beda`; `json_encode_int` json.fin:246, `json_decode_int` json.fin:507).
Only open Fin issue: **#50** (Linguist epic).

**Ecosystem** (see "Ecosystem ops"):

- **M1778/fin-sdk 3.2.0** — 23 vendored modules, `MANIFEST.md` pinned byte-exact to Fin
  `a6b4b67`; **now stale** for `lib/std/log.fin` + `lib/std/fs.fin` (changed by `e901ae5`/
  `da6d1f6`) — re-vendor (MINOR bump per policy) once `master` stabilizes.
  Release v3.2.0 live; `.github/workflows/provenance.yml` green.
- **M1778/Linf v3.0.0** — linter written in Fin against the SDK; four lanes
  (contract/gcc/actions/sarif) on **stderr**, stdout reserved; 4 of 5 rules live; `tests/run.sh`
  **28 asserts** green; self-check CI green; release carries a verified `linf-linux-x86_64`.
- **Pages**: https://m1778.github.io/Fin/ — workflow mode, single deploy channel
  (`.github/workflows/deploy-pages.yml`), run 39369866809 green.
- **Releases**: fin-sdk v3.2.0, Linf v3.0.0. **Fin `v0.4.0` NOT cut** — blocked by the Gate.

## GATE — the four blockers for `v0.4.0`

CI run **39398534233** (`master` @ `d5d25b7`) verdict — Linux and toolchain lanes green, four
lanes red; the failures are behavioral, not build failures (the MSVC compile bug is fixed, so
Windows suites now run):

| Lane | Result |
| --- | --- |
| linux-x86_64, linux-arm64, build.sh, perf (optional), sanitizers (optional) | **green** |
| macos-x86_64, macos-arm64 | **red — 2 tests** (`Soundness_Fs.*`) |
| windows-x86_64, windows-arm64 | **red — 12 tests** (Derive 4, Log 5, Fs 2, ContractCorpus 1) |

### B1 — macOS: `walk_dir` links against a non-existent symbol

`Soundness_Fs.EveryCallRunsInATmpdirAndNothingTouchesTheCwd`,
`Soundness_Fs.AMissingPathIsRoutineAndNeverAFault` — compile/link failure:

```
Undefined symbols for architecture arm64:
  "_getdirentries64", referenced from: _walk_dir in *.fin.o
```

`getdirentries64` is **not a public macOS symbol**; the `da6d1f6` rewrite is unlinkable on both
mac arches (its local Linux proof could not see this). Required: rewrite `walk_dir`'s record
reading on a *public* macOS API (`readdir` family is the candidate) with per-ABI-correct record
access, or another linkable approach; keep Linux output byte-identical and keep stage-vs-ref
runtime identity (stdout **and** stderr). The true prior defect was a `d_reclen`/inode-layout
misread on x86_64 — solve layout, not by inventing symbols. Tests to turn green are exactly the
two above; add no new macOS-only test silencers.

### B2 — Windows: `derive_hash` truncates to 32 bit (LLP64)

`Soundness_Derive.{TheWrapperBindsFromTheValue, EveryByteOfEveryFieldEntersTheMix,
AnEmptyStructHasNothingToMix, ANestedStructWalksItsOwnFields}`:

```
run.out  = "3300229000"             (got, < 2^32)
expected = "2952561617234395016"    (64-bit FNV value)
```

`ulong` is 32-bit on Windows (LLP64) and 64-bit on Linux — the hash arithmetic silently wraps.
Fix the width at the owning layer with explicit 64-bit operations (not by weakening the test),
then confirm the Linux expected bytes are unchanged. **Record the cross-platform-stability
decision** (the tests imply identical hashes across platforms): an ADR 0058 addendum line, so the
guarantee is ruled rather than incidental.

### B3 — Windows: `log.fin` pulls POSIX-only libc symbols

`Soundness_Log.{TextModeEmitsToStderrAndNeverToStdout, TheFilterRanksAndTheEnvOverrideWins,
ABadLogLevelBlamesAndNamesTheValue, JsonlModeEmitsTheFincContractShape,
TheTimestampPrefixRidesByDefault}` and `Soundness_Fs.{EveryCallRunsInATmpdirAndNothingTouchesTheCwd,
AMissingPathIsRoutineAndNeverAFault}` — link failure:

```
lld-link: error: undefined symbol: setenv
lld-link: error: undefined symbol: dprintf
```

POSIX-only externs used by the log path (fs's bundled-program tests pull the same dependency).
Fix inside the library (platform lane or a Windows-compatible call such as `_putenv_s` /
`_dprintf` / `fprintf(stderr, …)`) so the contract bytes on stderr stay identical; do **not**
gate or delete the tests. Together with B1/B2 this restores "every shipped module links on every
required lane".

### B4 — Windows: contract corpus path substitution

`ContractCorpus.EveryCaseReplaysAgainstTheRealCompiler` — every case diverges because the
diagnostic `file` is emitted as the platform path (`D:/a/Fin/Fin/tests\contract-corpus\…`) while
the expected file holds `@DIR@/input.fin`. This is a **harness normalization** defect (separator
and drive substitution), not a compiler behavior change: normalize both sides in
`tests/test_infra_contract.cpp` (and the `tests/tools/contract_corpus_regen.sh` substitution)
using platform-independent path normalization; keep the corpus expectation files stable, and
verify the contract's `file` semantics stay as documented. Do not "fix" this by changing finc to
emit POSIX separators on Windows unless the contract ruling changes — the contract echoes paths.

**After all four are green on all six required lanes on `master`:** cut the release —
tag must be `v0.4.0` (matches `CMakeLists.txt` `project(FinCompiler VERSION 0.4.0)` and
`finc 0.4.0 (contract 3)`); `release.yml` fires on the tag and its full matrix must pass.
A notes draft existed at `/tmp/opencode/fin-release-notes.md` (scratch; re-create under
`~/fin-tmp/` if absent) — it must state the honest linking story and the four platform fixes.
Then re-vendor fin-sdk (§ Ecosystem ops) and re-pin Linf's self-check.

## Local build and verification

```bash
./build.sh                                   # pinned LLVM major (from CMakeLists.txt)
cmake --build build -j4                      # rebuild before any gtest run after .cpp edits
ctest --test-dir build --output-on-failure   # 2931 tests, ~7 min
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
- **Enum value representation** — `Ok(T)` denotes the member (ADR 0037); lowering waits.
- **Nullable struct-field layout** — `nullifier.fin` (`b? <int>`) still refused in codegen.
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
- **LLP64:** `ulong`/`long` are 32-bit on Windows. Anything that hashes, masks, or packs must
  use explicitly wide arithmetic or it silently truncates.
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

# release once every required lane is green
gh release create v0.4.0 --target master --title "finc 0.4.0 (contract 3)" --notes-file <notes>
gh run watch "$(gh run list -R M1778/Fin --workflow=release.yml --limit 1 --json databaseId -q '.[0].databaseId')" -R M1778/Fin
```

## Definition of done for this handoff

The receiving agent has finished the release gate when: all six required lanes are green on
`master`; `v0.4.0` exists and `release.yml` completed green; fin-sdk is re-vendored to the new
`master` with provenance green and Linf re-pins against it; `check_docs.py` passes; this file's
"Verified state" and "GATE" sections are rewritten to the new reality (with fresh
measurements); and the owner decisions above are either recorded or explicitly listed as
still-open. If a lane cannot go green, the blocker must be named, evidenced, and owned — never
worked around silently.
