# Compiler benchmarks

Micro-benchmark corpus + driver behind `docs/PERFORMANCE_NOTES.md` §2.

| File | What |
|---|---|
| `fib.fin` / `fib.c` / `fib.cpp` / `fib.rs` | Recursive fibonacci, n=40 |
| `qsort_big.fin` / `qsort.c` / `qsort.cpp` / `qsort.rs` | Quicksort, N=1M |
| `mmul_big.fin` / `mmul.c` / `mmul.cpp` / `mmul.rs` | Matrix multiply, N=200, i,k,j order |
| `run_bench.py` | Driver: median-of-N compile + runtime, env header, JSON output |
| `baseline.json` | Reference numbers (machine-local; informational only) |

Run:

```bash
python3 bench/run_bench.py [--json bench/results.json]
# FINC=build/finc is the default; override with FINC=/path/to/finc
```

Rules: deltas under ~5% are machine noise, never regressions. The CI
`perf` job (`continue-on-error`) uploads fresh numbers as an artifact —
signal, not gate. Exit 1 means a Fin source failed to compile or run;
a missing gcc/g++/rustc is a SKIP, not a failure.
