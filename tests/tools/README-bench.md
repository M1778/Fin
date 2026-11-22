# Compile-time benchmark

`bench_compile.py` measures what `finc` costs to compile a fixed set of inputs, and
`--compare` gate a change against a recorded baseline. It exists because every compile-time
claim in this repository used to be made by hand, on one machine, at one moment — and that is
how a 3.6x Release-vs-Debug difference went unnoticed.

This is not `bench/run_bench.py`. That one measures generated programs' *runtime*; this one
measures the *compiler*.

## Run it

```sh
python3 tests/tools/bench_compile.py                        # measure, print the table
python3 tests/tools/bench_compile.py --runs 11              # tighter numbers, slower
python3 tests/tools/bench_compile.py --save-baseline        # record the reference
python3 tests/tools/bench_compile.py --compare              # gate against it
```

Choose the binary with `--finc PATH` or `FINC`. Without one it picks `build-rel/finc`, then
`build/finc`, then `build/Release/finc`, then `finc` on `PATH`. The path it used is printed.

Exit status: `0` measured and within threshold (or `GATE INCONCLUSIVE`: nothing reproducible
moved, but some rows could not be judged); `1` a scenario failed to run, or the binary's build
type is refused; `2` `--compare` found a reproducible regression — or a reproducible
improvement, which must be locked into the baseline. A comparison *refused* for being
incomparable is `0`, not `2` — a different machine says nothing about the compiler, so there is
nothing to fail.

## What is measured

Everything under `bench-inputs/`. Nothing is taken from `tests/samples/`: those files are the
language specification (ADR 0008), and timing them would make a performance edit change what
the spec tests.

| Rows | What it tells you |
| --- | --- |
| `startup/--version` | The process floor. Nothing can be faster than this. |
| `startup/tiny` | Pipeline cost with no imports: read, parse, analyse, no module walk. |
| `stdlib/<module>` | One module imported alone. 49 rows, so a change to one module's cost is visible by name instead of averaged into a total. |
| `program/check` | A realistic six-import program, type-checked only. |
| `program/build` | The same program through `-o`: codegen and linking added. |

The paired `program/check` / `program/build` rows are the point of having both: a change that
speeds up the front end moves the check row and leaves the build row alone, while a codegen
change does the reverse. Without the pair, either change looks like "the compiler got faster".

`program/build` writes its executable into a temporary directory that is deleted afterwards, so
a run leaves nothing in the tree.

## Why the minimum, not the mean

Every row is the wall time of a whole process, and many are ~12 ms. On a shared machine a mean
of N such runs mostly measures how much of the CPU other work took, not how long the compiler
needs: one preemption adds a fixed delay to one sample and drags the mean up, while the fastest
run is the one that got a clean window. That is the number closest to the compiler's own cost.

Both are recorded and printed so the gap between them is visible. A min and a median that
diverge by more than a few percent mean the machine was busy and the run should be repeated,
not that the compiler is fast or slow.

The scenarios are timed **round-robin** — every scenario once per iteration, not all N runs of
one scenario in a row. A full run takes a minute or two, and a machine that quietens down in
the middle of it should benefit every scenario, not only whichever ones happened to be timed
then. Measured on the reference box (3 batches of min-of-3, 20 scenarios), worst-scenario spread
was 86% sequential against 49% interleaved while sibling builds held load at 1.4–5.2.

## The Debug trap

**A Debug build is not a slow compiler; it is a different, unoptimised program.** The same
source measured on the same machine:

| Scenario | Debug | Release | Ratio |
| --- | --- | --- | --- |
| `--version` | 7.2 ms | 7.3 ms | 1.0x |
| check, no imports | 29.3 ms | 12.2 ms | 2.4x |
| check, 10 stdlib modules | 159.9 ms | 44.8 ms | **3.6x** |
| build `-o` | 235.0 ms | 113.9 ms | 2.1x |

So the harness reads `CMAKE_BUILD_TYPE` out of the `CMakeCache.txt` above the binary and
**refuses to run against Debug**, or against a binary whose build type it cannot read, unless
`--allow-debug` is passed. The refusal is the most important behaviour in the file: measuring a
Debug compiler and reporting it as the compiler's speed is exactly the mistake this tool exists
to prevent.

`--allow-debug` exists because measuring Debug is occasionally the right question (is the debug
build getting slower?). If you use it, label the result as Debug wherever you report it — it is
in the JSON, so the label travels with the number.

It also warns when the binary is older than the newest file in `src/`. Same class of error: the
number is real, but it belongs to different code than the tree you are looking at.

## Threshold, and when `--compare` refuses

The default threshold is **10%**. It is recorded in the baseline, so a noisier machine can carry
its own value; `THRESHOLD_PCT` in the script is only the default used when the baseline has none.

Why 10%, measured rather than assumed: on the reference box (16-core i5-14400F), measuring the
same Release binary against itself — min-of-5, 53 scenarios, load 1.4–2.2 from sibling builds —
the median deviation was 2.9% and the maximum 14.9%. So 10% is wider than the quiet-machine
noise and narrower than any regression worth reporting. It is also far below the 2.1x–3.6x that
a build type or a real structural change produces, which is the range this gate must catch.

Because this box *was* noisy, the harness does not rely on the threshold alone:

- **Refusals happen before measuring.** A baseline carries the fingerprint it was recorded with
  (CPU model, core count, OS, arch, build type) and the load average envelope it was measured
  inside. If either differs, `--compare` prints `NOT COMPARABLE`, names which field differs,
  explains why, and exits `0` without timing anything. A cross-machine delta is not a faster or
  slower compiler, and printing one would invent the fact this tool exists to prevent.
- **A suspect is re-measured.** If a scenario reads beyond the threshold in either direction,
  only the suspects are timed again and judged on the second batch. A real change reproduces; a
  scheduling spike does not. Without this, a single busy minute on a shared box fails the gate on
  its own.
- **Every row reports its own noise.** `noise_pct` is the spread of the minima of four time
  blocks within the run — how much the machine's mood moved while measuring. A row that moved
  beyond the threshold but was itself only reproducible to the threshold is reported as
  `unresolved` and does not fail the gate; the run exits `0` with an explicit
  `GATE INCONCLUSIVE` line instead of inventing a verdict. Raise `--runs` or find a quieter
  machine to resolve it.

A regression **and a reproducible improvement** both fail `--compare`. A win is not a pass:
leaving the baseline alone means the next change of the same size reads as "no change", and the
gap between baseline and reality silently absorbs every later regression. Re-record with
`--save-baseline` to lock the improvement in.

A baseline is a file, not a rule: `--baseline PATH` compares against any of them. Use one per
machine if you have more than one.

## Files

| File | What |
| --- | --- |
| `bench_compile.py` | The harness. |
| `bench-baseline.json` | The measured reference, with the fingerprint and load envelope that produced it. |
| `bench-inputs/startup/` | `tiny.fin`: no imports, the pipeline floor. |
| `bench-inputs/stdlib/` | One file per module of `lib/std/`, each importing that module alone. |
| `bench-inputs/program/app.fin` | The realistic multi-import program. |

`bench-inputs/` is excluded from `apply_header.py` on purpose. The 30-line licence header would
be read by the compiler on every scenario and would change every number; these are timing
fixtures, in the same class as `tests/contract-corpus/`, not part of the licensed program. Do not
"fix" the missing header — it is the point.

Adding a scenario is a deliberate act: it changes the baseline for every machine. Add the input
under `bench-inputs/`, add the row to `scenarios()`, re-record the baseline, and mention both in
the commit.

## CI

`.github/workflows/ci.yml` has a `compile-bench` job that builds Release, runs `--compare`, and
uploads the numbers. It is `continue-on-error` today. See the comment on the job for exactly
what would make it blocking.
