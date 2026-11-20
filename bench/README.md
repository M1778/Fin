# Compiler benchmarks

Micro-benchmark corpus + driver behind `docs/PERFORMANCE_NOTES.md` §1 ("The Gate").

| File | What |
|---|---|
| `fib.fin` / `fib.c` / `fib.cpp` / `fib.rs` | Recursive fibonacci, n=40 |
| `qsort_big.fin` / `qsort.c` / `qsort.cpp` / `qsort.rs` | Quicksort, N=1M |
| `mmul_big.fin` / `mmul.c` / `mmul.cpp` / `mmul.rs` | Matrix multiply, N=200, i,k,j order |
| `run_bench.py` | Driver: median-of-N compile + runtime, env header, JSON output |
| `baseline.json` | Older single-machine reference numbers (informational, ungated) |
| `baselines/*.json` | The gated baselines: fingerprint, method, tolerance, metrics |
| `make_baseline.py` | Writes a `baselines/` file from `run_bench.py` result files |
| `check_bench.py` | Gates a result file against the baseline for this machine |

`bench/baseline.json` predates the gate and is kept as historical reference;
`bench/baselines/` is what `check_bench.py` reads and what CI compares against.

Run:

```bash
python3 bench/run_bench.py --repeat 3 --json /tmp/bench.json
# FINC defaults to build/finc; override with FINC=/path/to/finc
python3 bench/check_bench.py --results /tmp/bench.json
```

`run_bench.py` alone measures; it never fails on a slow number. Exit 1 means a
Fin source failed to compile or run (a missing gcc/g++/rustc is a SKIP).

`check_bench.py` exits 2 on a Fin metric more than `gate.tolerance_pct` slower
than baseline, 1 if the result file lacks a gated metric, and 0 when it passes
or when this machine has no comparable baseline. Incomparable and
overloaded-box runs SKIP: a different machine, or one whose 1-minute load
average at run start exceeded `gate.max_loadavg`, says nothing about the
compiler.

Rules: deltas under ~5% are machine noise, never regressions. The checked-in
baseline's tolerance is 20%, measured from run-to-run spread on the baseline
machine — see `docs/PERFORMANCE_NOTES.md` "The Gate" for the data and the
known limits (shared-runner noise, WSL2 vs native variance).
