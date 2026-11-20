#!/usr/bin/env python3
"""Compile-time + runtime benchmark for Fin vs C/C++/Rust.

One-command repro for docs/PERFORMANCE_NOTES.md "The Gate":
    python3 bench/run_bench.py [--repeat 3] [--json bench/results.json]

Method: median of N wall-clock runs (default 5 compile, 3 runtime);
min is printed alongside so results stay comparable with the
min-of-11 tables in PERFORMANCE_NOTES.

--repeat N runs the whole suite N times and reduces each metric to the min of
its N medians. That reduction is what bench/check_bench.py compares against
bench/baselines/, so a gated run must pass --repeat with the same N the
baseline recorded.

Exit status: 0 when every Fin source compiles and runs; 1 when a Fin
source fails to compile or a built binary crashes. A missing
gcc/g++/rustc is a SKIP, not a failure (CI runners may lack rustc).
This script never fails on a slow number; the gate is check_bench.py.
"""
import argparse
import datetime
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
    release = platform.uname().release.lower()
    info = {
        "platform": platform.platform(),
        "family": platform.system().lower(),
        "virt": "wsl2" if ("microsoft" in release or "wsl" in release) else "native",
        "cpu": platform.machine(),
        "arch": platform.machine(),
        "cpu_count": os.cpu_count(),
        "python": platform.python_version(),
        "flags": "gcc/g++ -O2, rustc -O, finc -O2",
        "loadavg": load_avg(),
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
    info["finc_build_type"] = finc_build_type(info["finc_path"])
    try:
        info["finc_mtime"] = datetime.datetime.fromtimestamp(
            os.path.getmtime(info["finc_path"])).astimezone().isoformat(timespec="seconds")
    except OSError:
        info["finc_mtime"] = "unknown"
    try:
        with open(os.path.join(REPO_ROOT, "CMakeLists.txt")) as f:
            for line in f:
                if "FIN_LLVM_MAJOR" in line and "set(" in line:
                    info["llvm_pin"] = line.strip()
                    break
    except OSError:
        pass
    return info


def load_avg():
    try:
        with open("/proc/loadavg") as f:
            return float(f.read().split()[0])
    except (OSError, ValueError, IndexError):
        return None


def finc_build_type(finc_path):
    """Debug and Release finc are not interchangeable for timing; a baseline is
    only comparable against the same one. Read it from the CMake cache next to
    the binary (build/CMakeCache.txt or build/<dir>/<cfg>/CMakeCache.txt)."""
    d = os.path.dirname(os.path.abspath(finc_path))
    for _ in range(4):
        cache = os.path.join(d, "CMakeCache.txt")
        if os.path.isfile(cache):
            try:
                with open(cache) as f:
                    for line in f:
                        if line.startswith("CMAKE_BUILD_TYPE:"):
                            return line.split("=", 1)[1].strip() or "unknown"
            except OSError:
                break
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return "unknown"


def git_head():
    try:
        r = subprocess.run(["git", "-C", REPO_ROOT, "rev-parse", "HEAD"],
                           capture_output=True, text=True, timeout=30)
        return (r.stdout or "").strip() or "unknown"
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"


def run_once(finc, out_dir, compile_runs, run_runs):
    """One full pass. Returns (failed, per-benchmark results)."""
    failed, out = False, {}
    binaries = {}  # (bench, lang) -> path

    print("\n=== COMPILATION TIME (-O2, median of %d) ===" % compile_runs)
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
            med, best, err = measure(cmd, compile_runs)
            status = "ok" if med is not None else ("SKIP" if "not found" in err else "FAIL")
            print("  %-4s %s (min %s)%s" % (lang + ":", fmt(med), fmt(best),
                                            "" if status == "ok" else "  [%s %s]" % (status, err)))
            row[lang] = {"median_s": med, "min_s": best, "status": status, "detail": err}
            if med is not None:
                binaries[(stem, lang)] = cmd[cmd.index("-o") + 1]
            elif lang == "Fin":
                failed = True
        out.setdefault(stem, {})["compile"] = row

    print("\n=== RUNTIME (median of %d) ===" % run_runs)
    for name, stem, *_ in BENCHMARKS:
        print("\n--- %s ---" % name)
        row = {}
        for lang in ("C", "C++", "Rust", "Fin"):
            binary = binaries.get((stem, lang))
            if not binary:
                print("  %-4s SKIPPED (not built)" % (lang + ":"))
                row[lang] = {"median_s": None, "min_s": None, "status": "SKIP", "detail": "not built"}
                continue
            med, best, err = measure([binary], run_runs, timeout=600)
            status = "ok" if med is not None else "FAIL"
            out_txt = ""
            if med is not None:
                try:
                    r = subprocess.run([binary], capture_output=True, text=True, timeout=600)
                    out_txt = (r.stdout or "").strip().splitlines()[-1] if r.stdout else ""
                except (OSError, subprocess.TimeoutExpired):
                    pass
            else:
                failed = failed or lang == "Fin"
            print("  %-4s %s (min %s)  output=%s%s" % (lang + ":", fmt(med), fmt(best), out_txt,
                                                       "" if status == "ok" else "  [FAIL %s]" % err))
            row[lang] = {"median_s": med, "min_s": best, "status": status,
                         "detail": err, "output": out_txt}
        out.setdefault(stem, {})["runtime"] = row
    return failed, out


def main():
    ap = argparse.ArgumentParser(description="Fin vs C/C++/Rust benchmark")
    ap.add_argument("--compile-runs", type=int, default=5)
    ap.add_argument("--run-runs", type=int, default=3)
    ap.add_argument("--out-dir", default=None, help="binary dir (default: fresh temp dir)")
    ap.add_argument("--json", default=None, help="write results JSON here")
    ap.add_argument("--repeat", type=int, default=1,
                    help="repeat the whole measurement N times; each metric in "
                         "'metrics' is the min of its N medians (the gate protocol)")
    a = ap.parse_args()

    finc = find_finc()
    out_dir = a.out_dir or tempfile.mkdtemp(prefix="fin-bench-")
    os.makedirs(out_dir, exist_ok=True)
    cleanup = a.out_dir is None

    info = machine_info()
    print("=== ENVIRONMENT ===")
    for k, v in info.items():
        print("  %s: %s" % (k, v))

    failures, passes = 0, []
    for i in range(a.repeat):
        if a.repeat > 1:
            print("\n########## PASS %d/%d ##########" % (i + 1, a.repeat))
        failed, out = run_once(finc, out_dir, a.compile_runs, a.run_runs)
        failures += bool(failed)
        passes.append(out)

    # The gate protocol: per metric, the min of the pass medians. A shared runner's
    # noise is one-sided (other tenants only add time), so min-of-N is the stable
    # estimator; each pass still reports its own median above.
    metrics = {}
    for stem in (b[1] for b in BENCHMARKS):
        metrics[stem] = {}
        for phase in ("compile", "runtime"):
            metrics[stem][phase] = {}
            langs = set()
            for p in passes:
                langs.update(p.get(stem, {}).get(phase, {}))
            for lang in langs:
                samples = [p.get(stem, {}).get(phase, {}).get(lang, {}).get("median_s")
                           for p in passes]
                samples = [s for s in samples if s is not None]
                first = next((p.get(stem, {}).get(phase, {}).get(lang, {}) for p in passes
                              if p.get(stem, {}).get(phase, {}).get(lang)), {})
                metrics[stem][phase][lang] = {
                    "median_s": min(samples) if samples else None,
                    "pass_medians_s": samples,
                    "status": first.get("status", "SKIP"),
                    "output": first.get("output", ""),
                }

    results = {"environment": info, "git_head": git_head(),
               "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
               "repeat": a.repeat, "compile_runs": a.compile_runs,
               "run_runs": a.run_runs, "metrics": metrics,
               "benchmarks": passes[0]}

    print("\nNote: deltas under ~5% are machine noise, not regressions. See PERFORMANCE_NOTES section 1.")
    if a.repeat > 1:
        print("=== GATE METRICS (min of %d pass medians) ===" % a.repeat)
        for stem, phases in metrics.items():
            for phase, langs in phases.items():
                for lang, m in sorted(langs.items()):
                    if m["median_s"] is not None:
                        print("  %-9s %-8s %-4s %s" % (stem, phase, lang,
                                                        fmt(m["median_s"])))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=2)
        print("Wrote %s" % a.json)
    if cleanup:
        shutil.rmtree(out_dir, ignore_errors=True)
    failed = failures > 0
    print("=== %s ===" % ("DONE" if not failed else "DONE WITH FAILURES"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
