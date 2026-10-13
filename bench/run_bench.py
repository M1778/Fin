#!/usr/bin/env python3
"""Compile-time + runtime benchmark for Fin vs C/C++/Rust.

One-command repro for docs/PERFORMANCE_NOTES.md section 2:
    python3 bench/run_bench.py [--json bench/results.json]

Method: median of N wall-clock runs (default 5 compile, 3 runtime);
min is printed alongside so results stay comparable with the
min-of-11 tables in PERFORMANCE_NOTES. Deltas under ~5% are noise
(shared-runner timing) -- never gate on them.

Exit status: 0 when every Fin source compiles and runs; 1 when a Fin
source fails to compile or a built binary crashes. A missing
gcc/g++/rustc is a SKIP, not a failure (CI runners may lack rustc).
"""
import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(BENCH_DIR)


def find_finc():
    if os.environ.get("FINC"):
        return os.environ["FINC"]
    for rel in ("build/finc", "build/Release/finc", "build/Debug/finc"):
        cand = os.path.join(REPO_ROOT, rel)
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    return "finc"  # last resort: PATH


BENCHMARKS = [
    ("Fibonacci (n=40)", "fib", "fib.fin", "fib.c", "fib.cpp", "fib.rs"),
    ("Quicksort (N=1M)", "qsort_big", "qsort_big.fin", "qsort.c", "qsort.cpp", "qsort.rs"),
    ("Matrix Mul (N=200)", "mmul_big", "mmul_big.fin", "mmul.c", "mmul.cpp", "mmul.rs"),
]


def run_timed(cmd, timeout=300):
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        return None, "not found: " + cmd[0]
    except subprocess.TimeoutExpired:
        return None, "timeout"
    elapsed = time.perf_counter() - t0
    if r.returncode != 0:
        return None, (r.stderr or r.stdout or "exit %d" % r.returncode)[:200]
    return elapsed, ""


def measure(cmd, runs, timeout=300):
    samples, err = [], ""
    for _ in range(runs):
        t, err = run_timed(cmd, timeout)
        if t is None:
            return None, None, err
        samples.append(t)
    return statistics.median(samples), min(samples), ""


def fmt(t):
    return "FAIL" if t is None else "%.3f s" % t


def machine_info():
    info = {
        "platform": platform.platform(),
        "cpu": platform.machine(),
        "python": platform.python_version(),
        "flags": "gcc/g++ -O2, rustc -O, finc -O2",
    }
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    info["cpu"] = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    try:
        mem, swap = {}, {}
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                if k in ("MemTotal", "MemAvailable"):
                    mem[k] = v.strip()
                elif k in ("SwapTotal", "SwapFree"):
                    swap[k] = v.strip()
        info["memory"] = mem
        info["swap"] = swap
    except OSError:
        pass
    for tool, args in (("gcc", ["--version"]), ("g++", ["--version"]),
                       ("rustc", ["--version"]), ("finc", ["--version"])):
        path = find_finc() if tool == "finc" else shutil.which(tool)
        if not path:
            info[tool] = "missing"
            continue
        try:
            r = subprocess.run([path] + args, capture_output=True, text=True, timeout=30)
            info[tool] = (r.stdout or r.stderr).strip().splitlines()[0]
        except (OSError, subprocess.TimeoutExpired, IndexError):
            info[tool] = "unknown"
    info["finc_path"] = find_finc()
    try:
        with open(os.path.join(REPO_ROOT, "CMakeLists.txt")) as f:
            for line in f:
                if "FIN_LLVM_MAJOR" in line and "set(" in line:
                    info["llvm_pin"] = line.strip()
                    break
    except OSError:
        pass
    return info


def main():
    ap = argparse.ArgumentParser(description="Fin vs C/C++/Rust benchmark")
    ap.add_argument("--compile-runs", type=int, default=5)
    ap.add_argument("--run-runs", type=int, default=3)
    ap.add_argument("--out-dir", default=None, help="binary dir (default: fresh temp dir)")
    ap.add_argument("--json", default=None, help="write results JSON here")
    a = ap.parse_args()

    finc = find_finc()
    out_dir = a.out_dir or tempfile.mkdtemp(prefix="fin-bench-")
    os.makedirs(out_dir, exist_ok=True)
    cleanup = a.out_dir is None

    info = machine_info()
    print("=== ENVIRONMENT ===")
    for k, v in info.items():
        print("  %s: %s" % (k, v))

    failed, results = False, {"environment": info, "benchmarks": {}}
    binaries = {}  # (bench, lang) -> path

    print("\n=== COMPILATION TIME (-O2, median of %d) ===" % a.compile_runs)
    for name, stem, fin_src, c_src, cpp_src, rs_src in BENCHMARKS:
        print("\n--- %s ---" % name)
        row = {}
        jobs = []
        if shutil.which("gcc"):
            jobs.append(("C", ["gcc", "-O2", "-o", os.path.join(out_dir, stem + "_c"),
                               os.path.join(BENCH_DIR, c_src)]))
        else:
            print("  C:     SKIPPED (no gcc)")
        if shutil.which("g++"):
            jobs.append(("C++", ["g++", "-O2", "-o", os.path.join(out_dir, stem + "_cpp"),
                                 os.path.join(BENCH_DIR, cpp_src)]))
        else:
            print("  C++:   SKIPPED (no g++)")
        if shutil.which("rustc"):
            jobs.append(("Rust", ["rustc", "-O", "-o", os.path.join(out_dir, stem + "_rust"),
                                  os.path.join(BENCH_DIR, rs_src)]))
        else:
            print("  Rust:  SKIPPED (no rustc)")
        jobs.append(("Fin", [finc, os.path.join(BENCH_DIR, fin_src), "-o",
                             os.path.join(out_dir, stem + "_fin"), "-O2"]))
        for lang, cmd in jobs:
            med, best, err = measure(cmd, a.compile_runs)
            status = "ok" if med is not None else ("SKIP" if "not found" in err else "FAIL")
            print("  %-4s %s (min %s)%s" % (lang + ":", fmt(med), fmt(best),
                                            "" if status == "ok" else "  [%s %s]" % (status, err)))
            row[lang] = {"median_s": med, "min_s": best, "status": status, "detail": err}
            if med is not None:
                binaries[(stem, lang)] = cmd[cmd.index("-o") + 1]
            elif lang == "Fin":
                failed = True
        results["benchmarks"].setdefault(stem, {})["compile"] = row

    print("\n=== RUNTIME (median of %d) ===" % a.run_runs)
    for name, stem, *_ in BENCHMARKS:
        print("\n--- %s ---" % name)
        row = {}
        for lang in ("C", "C++", "Rust", "Fin"):
            binary = binaries.get((stem, lang))
            if not binary:
                print("  %-4s SKIPPED (not built)" % (lang + ":"))
                row[lang] = {"median_s": None, "min_s": None, "status": "SKIP", "detail": "not built"}
                continue
            med, best, err = measure([binary], a.run_runs, timeout=600)
            status = "ok" if med is not None else "FAIL"
            out = ""
            if med is not None:
                try:
                    r = subprocess.run([binary], capture_output=True, text=True, timeout=600)
                    out = (r.stdout or "").strip().splitlines()[-1] if r.stdout else ""
                except (OSError, subprocess.TimeoutExpired):
                    pass
            else:
                failed = failed or lang == "Fin"
            print("  %-4s %s (min %s)  output=%s%s" % (lang + ":", fmt(med), fmt(best), out,
                                                       "" if status == "ok" else "  [FAIL %s]" % err))
            row[lang] = {"median_s": med, "min_s": best, "status": status,
                         "detail": err, "output": out}
        results["benchmarks"].setdefault(stem, {})["runtime"] = row

    print("\nNote: deltas under ~5% are machine noise, not regressions. See PERFORMANCE_NOTES section 1.")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=2)
        print("Wrote %s" % a.json)
    if cleanup:
        shutil.rmtree(out_dir, ignore_errors=True)
    print("=== %s ===" % ("DONE" if not failed else "DONE WITH FAILURES"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
