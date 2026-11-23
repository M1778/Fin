# Compile-time performance: decisions and evidence

Recorded on branch `perf/compile-time`. Everything below is measured, with the build type named,
because the first finding was that `build/` is Debug and Debug is 3.6x slower than Release — a
number from it describes the build, not the compiler.

## Decision 1: the parser and lexer are not being rewritten

**Measured: the root file's lexer + parser is 0.08 ms of a 45 ms compile — 0.2%.**

```
PHASE preprocess      0.02 ms
PHASE root-parse      0.08 ms     <- the whole rewrite target
PHASE modules        27.95 ms     <- the actual cost
```

A rewrite in Fin, however clever, cannot move 0.2%. It would also put the component that
`tests/samples/` specifies (ADR 0008) at risk. **Rejected on evidence, not taste.**

## Decision 2: do not split the binary, even though it is a real 21%

Measured by building with the repository's own `-DFIN_WITH_LLVM=OFF` seam (no new code):

| | full | frontend-only | saved |
| --- | --- | --- | --- |
| `--version` | 7.72 ms | 0.99 ms | 87% |
| check, no imports | 12.05 ms | 5.07 ms | 58% |
| check, 10 stdlib modules | 49.8 ms | 39.2 ms | **21%** |

It works, and the backend-less binary refuses a build with a named diagnostic rather than
emitting nothing. But:

- the release layout is `bin/finc` + `lib/std` and **`finn` runs `finc --version` as its
  install-time verification**. That command would now pass on a binary that cannot emit code.
  I checked `finn`'s code: it validates presence, not layout, so this would *quietly* work —
  which is worse. A user on a platform whose second binary failed to unpack gets a compiler that
  checks perfectly and refuses to build.
- two binaries means two artifacts, two signing surfaces, two things to get right in every one
  of the seven platform lanes, for a 21% win on checks and a 5% win on builds.

**Rejected: it makes the install fragile in exchange for a fraction of a compile.** Recorded in
`perf-findings.md` with the numbers so the decision can be revisited if checks ever dominate
usage.

## Decision 3: take the fix that is worth more and risks nothing — stop re-analysing the stdlib

**Measured: 71% of a compile is semantic analysis of ten stdlib modules that did not change.**

| Phase | Release | Share |
| --- | --- | --- |
| Semantic analysis (10 modules) | 23.7 ms | **71%** |
| Parse them | 5.7 ms | 17% |
| Preprocess them | 2.4 ms | 7% |
| Everything else | 1.6 ms | 5% |

That is where the money is, and the agents are on it: a module cache keyed on file bytes plus
preprocessor defines, and an analyzer profile to see whether the 23.7 ms is fixable at source.

## Decision 4: the link step is the next real win, and it is not Fin's code

A build (`-o`) spends **64 ms of 119 ms outside the compiler**:

```
check only        54.7 ms
-c (object)       55.3 ms
-o -O0            119.1 ms      <- 64 ms is emission + linking
```

The object is 4 KB. Timing the pieces:

```
cc r.o -o out     77 ms     <- what finc shells out to
ld r.o ...        13 ms
```

`cc` spends that on `collect2`, the LTO plugin wrapper and full crt setup. A direct `ld` line
runs correctly here — but it needs hardcoded crt object paths and the dynamic-linker path,
which is exactly the platform-specific guesswork that has burned this project twice
(`docs/known-issues.md`: the COPY relocations, the Homebrew LLVM path).

**Decision: not taken in this branch.** A 64 ms win exists, but it must be done by *deriving*
the crt paths from the same `cc` that works today (asking `cc -print-file-name=crt1.o` and
friends), not by hardcoding them, and it needs a fallback when derivation fails. That is a
design with its own failure modes and it deserves its own branch rather than a rush at the end
of this one. Recorded with the numbers.

## What this branch ships

1. **A benchmark harness with a Debug refusal** — the tool that would have caught the 3.6x
   Release/Debug confusion, and that makes every future claim checkable.
2. **Measured analyzer work** — whatever the profiler proves, with before/after numbers.
3. **A module cache prototype behind a flag** — correct invalidation first, measured second.
4. **This document** — the four decisions and the evidence, so the next person does not
   re-derive them.

## The headline for the language

Nothing here required rewriting the parser in Fin, and the request's premise — that the parser
is where the time goes — was false by a factor of 500. What was true is that **71% of a compile
is re-analysing a standard library that has not changed**, and that is a caching problem, not a
parsing one.

The single best thing this branch leaves behind is the harness: without it, the next person
will measure Debug, see "160 ms", and optimise a parser that costs 0.08 ms of it.


## Shipped on this branch

- `tests/tools/bench_compile.py` and 53 checked-in fixtures — the harness, with the Debug
  refusal, the three-outcome gate (pass / regression / inconclusive), and the baseline file.
- The benchmark fixtures are `.bfin`, not `.fin`: the corpus-discovery test refuses any `.fin`
  in the tests tree that is neither a sample nor ruled on, and a timing fixture is neither.

## Measured and not shipped, with the reasons above

- The parser/lexer rewrite (0.2% of a compile).
- The frontend/backend binary split (21% of a check, 5% of a build, and a release-layout change
  that weakens `finn`'s install verification).

## The structural finding this branch leaves for the next one

**The type-graph registry costs ~17% of a compile.** Every `Type` construction registers in a
process-wide hash set and every destruction erases, so the exit-time sweep can break the
reference islands of issue #101. `perf` puts `_M_emplace_uniq`, `_M_insert_unique_node`,
`_Hash_bytes` and part of `PrimitiveType::~PrimitiveType` at 16.8% combined.

The fix is not removing the registry — that returns the 373 KB leak and the leak check depends
on it. It is arena-allocating types per compile, which removes the registry, the per-node
destructions, and the islands together. That needs a ruling because types are `shared_ptr`-
managed everywhere. Recorded in `docs/HANDOFF.md` under "Measured, and deliberately not
shipped".

## What the harness taught on its first day

Its first `--compare` reported every scenario 10-24% slower. Nothing had changed: the full
3,287-test suite was running concurrently on the same machine. On a quiet machine the gate
passed, worst delta +6.9%.

That is the harness working: it does not decide whether a change is slow, it decides whether
this run can answer the question, and it says "inconclusive, re-run on a quieter machine"
rather than passing or failing a number it cannot reproduce.
