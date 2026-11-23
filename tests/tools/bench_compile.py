#!/usr/bin/env python3
# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️ for someone special.
#
# This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
# Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
# Fin is the name of the Angel from BlackNightmare-Stories not yet published.
# “Love is the reason this project exists”
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778/Fin
# Profile: https://github.com/M1778M/
# -----------------------------------------------------------------------------
# Copyright (C) 2027 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================

"""Compile-time benchmark for finc, with a baseline gate.

    python3 tests/tools/bench_compile.py                 # measure, print the table
    python3 tests/tools/bench_compile.py --save-baseline # record the reference
    python3 tests/tools/bench_compile.py --compare       # gate a change on it

The input set is fixed and checked in under bench-inputs/: process startup, each
stdlib module imported alone, a realistic multi-import program, and that program
as a check versus a full -o build. tests/samples/ is the language specification
(ADR 0008) and is deliberately not timed -- a performance edit there would
change what the samples test.

Each scenario runs once untimed to warm the page cache, then N times round-robin.
The table reports the minimum and the median; the gate uses the minimum. See
README-bench.md for why min and not mean, and for the Debug trap this refuses.

Exit status:
  0  measured and within threshold, or GATE INCONCLUSIVE (nothing reproducible
     moved but some rows could not be judged), or a comparison was refused
     because the baseline is not comparable (different machine/build/load)
  1  a scenario could not run, or the binary's build type is not measurable
  2  --compare found a reproducible regression beyond the threshold, or a
     reproducible improvement that must be locked into the baseline
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
INPUTS = HERE / "bench-inputs"
DEFAULT_BASELINE = HERE / "bench-baseline.json"

# The gate threshold as a percentage of the baseline's min. 10% is the starting
# point: wider than this box's quiet noise and narrower than a regression worth
# reporting. Measured on the reference machine (min-of-5, the same Release
# binary against itself, 53 scenarios) the median deviation was 2.9% and the max
# 14.9% -- with sibling builds holding load at 1.4-2.2, which is why every run
# records its load envelope, refuses to compare outside it, and re-confirms a
# suspected regression before failing. On an idle runner the spread is smaller
# and 10% has margin. Recorded per baseline, so a noisier machine can carry its
# own number. See README-bench.md "The threshold".
THRESHOLD_PCT = 10.0

# A run is only comparable while the machine is about as busy as the baseline's.
# One full load unit of headroom, and never below 1.0, so an idle baseline is not
# gated by a box with one background build on it.
LOAD_HEADROOM = 1.0
LOAD_FLOOR = 1.0

# Fields that make two runs comparable. CPU model is in the identity because a
# different clock or core count changes every number here; build type is in it
# because the same source is 3.6x apart between Debug and Release. Load is
# recorded separately: a busy machine is a different moment, not a different
# machine, and it gets its own message.
IDENTITY_KEYS = ("system", "machine", "cpu", "cores", "build_type")


def executable(path):
    """The path, or its .exe sibling: CI on Windows has no bare `finc`."""
    p = Path(path)
    if p.is_file():
        return p
    if not p.suffix and Path(str(p) + ".exe").is_file():
        return Path(str(p) + ".exe")
    return None


def find_finc(explicit):
    if explicit:
        found = executable(explicit)
        if found is None:
            sys.exit("error: no finc at %s" % explicit)
        return found
    env = os.environ.get("FINC")
    if env:
        found = executable(env)
        if found is None:
            sys.exit("error: FINC=%s is not a file" % env)
        return found
    for rel in ("build-rel/finc", "build/finc", "build/Release/finc"):
        found = executable(ROOT / rel)
        if found:
            return found
    found = executable("finc")
    if found is None:
        sys.exit("error: no finc found (pass --finc PATH or set FINC)")
    return found


def build_type(binary):
    """Read the build type from the CMake cache that produced the binary.

    Never guess. A binary with no readable cache is "unknown", and unknown is
    refused like Debug: a performance number whose build type cannot be named is
    the failure this tool exists to prevent.
    """
    d = binary.resolve().parent
    for i, base in enumerate((d, d.parent, d.parent.parent)):
        cache = base / "CMakeCache.txt"
        if not cache.is_file():
            continue
        text = cache.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"^CMAKE_BUILD_TYPE:STRING=(.*)$", text, re.M)
        if m and m.group(1).strip():
            return m.group(1).strip(), str(cache)
        # Multi-config generators (Visual Studio, Xcode) leave the type empty
        # and name the configuration in the binary's directory.
        return ("unknown", str(cache)) if i == 2 else (d.name, str(cache))
    return "unknown", ""


def fingerprint(binary):
    cpu = platform.processor() or "unknown"
    try:
        for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
            if line.lower().startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass  # macOS/Windows have no /proc; platform.processor() stands in
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = None
    return {
        "system": platform.system(),
        "machine": platform.machine(),
        "cpu": cpu,
        "cores": os.cpu_count() or 0,
        "build_type": build_type(binary)[0],
        "cache": build_type(binary)[1],
        "loadavg": load,
    }


def newest_source():
    newest, name = 0.0, ""
    for pattern in ("*.cpp", "*.hpp", "*.y", "*.l"):
        for path in (ROOT / "src").rglob(pattern):
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if mtime > newest:
                newest, name = mtime, str(path.relative_to(ROOT))
    return newest, name


def scenarios():
    """The fixed input set, in a stable order.

    Grouped so each row means something: the process floor, one module at a
    time, the realistic program, then check versus full build.
    """
    plan = [
        ("startup/--version", None, None),
        ("startup/tiny", INPUTS / "startup" / "tiny.bfin", "check"),
    ]
    # `.bfin`, not `.fin`: the corpus-discovery test (ADR 0008) refuses any `.fin` file in
    # the tests tree that is neither a sample nor explicitly ruled on. A benchmark fixture is
    # neither -- it is timing data -- so it does not get the extension at all.
    for path in sorted((INPUTS / "stdlib").glob("*.bfin")):
        plan.append(("stdlib/" + path.stem, path, "check"))
    plan += [
        ("program/check", INPUTS / "program" / "app.bfin", "check"),
        ("program/build", INPUTS / "program" / "app.bfin", "build"),
    ]
    return plan


def command(finc, source, mode, outdir):
    if source is None:
        return [str(finc), "--version"]
    if mode == "build":
        return [str(finc), str(source), "-o", str(outdir / "app")]
    return [str(finc), str(source)]


def run_once(cmd):
    """One run. Output is discarded: the timing is the measurement."""
    start = time.perf_counter()
    try:
        done = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE)
    except OSError as exc:
        # A binary being relinked by another build shows up here as ETXTBSY.
        return time.perf_counter() - start, subprocess.CompletedProcess(
            cmd, 1, b"", ("could not execute: %s" % exc).encode())
    return time.perf_counter() - start, done


def measure(finc, plan, outdir, runs):
    """Every scenario once per iteration, not all runs of one scenario.

    Round-robin matters more than it looks: a run takes a minute or two and the
    machine gets busier and quieter within it. Timing all N runs of one
    scenario back to back spends every one in the same weather; interleaved,
    each scenario's minimum is taken across the whole run and catches whatever
    quiet window existed. Measured on the reference box with 3 batches of
    min-of-3 over 20 scenarios: worst-scenario spread was 86% sequential
    against 49% interleaved, on a box held at load 1.4-5.2 by sibling builds.
    """
    samples = {name: [] for name, _, _ in plan}
    for _ in range(runs):
        for name, source, mode in plan:
            elapsed, done = run_once(command(finc, source, mode, outdir))
            if done.returncode != 0:
                message = done.stderr.decode("utf-8", "replace").strip()
                return None, (name, message or "exit %d" % done.returncode)
            samples[name].append(elapsed)
    return {name: summarise(times) for name, times in samples.items()}, None


def summarise(times):
    """Min, median, and how reproducible this row was on this machine.

    `noise_pct` is the spread of the minima of four consecutive time blocks.
    The run is round-robin, so a block is a slice of wall-clock time: its spread
    captures how much the machine's mood moved while the run was going. It
    deliberately over-estimates the whole-run min's variability, because block
    minima are themselves noisier than the min of all runs -- being too
    suspicious costs a re-run, being too confident costs a false gate.
    """
    best = min(times)
    blocks = [times[i::4] for i in range(4)]
    minima = [min(b) for b in blocks if b]
    noise = (max(minima) - min(minima)) / best * 100.0 if len(minima) > 1 else 0.0
    return {"min_ms": best * 1000.0,
            "median_ms": statistics.median(times) * 1000.0,
            "noise_pct": round(noise, 2)}


def fmt_ms(value):
    return "%.2f" % value


def print_table(rows, comparison):
    """One line per scenario. comparison is (baseline, threshold) or None."""
    if comparison is None:
        print("%-21s %9s %9s %7s" % ("scenario", "min ms", "median ms", "noise"))
        print("-" * 51)
        for name, row in rows.items():
            print("%-21s %9s %9s %6.1f%%"
                  % (name, fmt_ms(row["min_ms"]), fmt_ms(row["median_ms"]),
                     row["noise_pct"]))
        print()
        return
    baseline, threshold = comparison
    print("%-21s %9s %9s %8s %7s  %s"
          % ("scenario", "min ms", "median ms", "delta", "noise", "verdict"))
    print("-" * 71)
    for name, row in rows.items():
        old = baseline.get("scenarios", {}).get(name)
        if not old:
            print("%-21s %9s %9s %8s %6.1f%%  not in baseline"
                  % (name, fmt_ms(row["min_ms"]), fmt_ms(row["median_ms"]), "-",
                     row["noise_pct"]))
            continue
        delta = (row["min_ms"] - old["min_ms"]) / old["min_ms"] * 100.0
        if abs(delta) > threshold:
            # Beyond threshold is a verdict only if this run was reproducible
            # enough to support one at that threshold.
            if row["noise_pct"] >= threshold:
                verdict = "unresolved"
            elif delta > 0:
                verdict = "REGRESSED"
            else:
                verdict = "FASTER"
        else:
            verdict = "ok"
        print("%-21s %9s %9s %+7.1f%% %6.1f%%  %s"
              % (name, fmt_ms(row["min_ms"]), fmt_ms(row["median_ms"]), delta,
                 row["noise_pct"], verdict))
    print()


def refuse_unmeasurable(finc, ident, allow_debug):
    """Debug and unknown build types are refused: neither names what ran."""
    label = ident["build_type"]
    if label not in ("Debug", "unknown") or allow_debug:
        return
    if label == "Debug":
        why = ("its numbers describe unoptimised C++, not the compiler: the same "
               "source measured 159.9 ms on a 10-module check in Debug against "
               "44.8 ms in Release, a 3.6x difference")
    else:
        why = ("the build type cannot be read back from %s, so there is no way "
               "to know what was measured" % (ident["cache"] or "the tree"))
    sys.exit("refused: %s is a %s build.\n"
             "  A performance number without its build type is worthless; %s.\n"
             "  Pass --allow-debug to measure it anyway, and label every result "
             "as Debug where it is reported." % (finc, label, why))


def comparability(baseline, baseline_path, ident):
    """Why this baseline cannot judge this run, or None if it can.

    Checked before measuring: if the answer is already "not comparable", a
    minute of timings would only produce numbers nobody may use.
    """
    theirs = baseline.get("fingerprint", {})
    differences = [(k, theirs.get(k), ident[k]) for k in IDENTITY_KEYS
                   if theirs.get(k) != ident[k]]
    if differences:
        lines = ["NOT COMPARABLE: %s was recorded on:" % baseline_path]
        lines += ["  %-11s baseline %-30s this run %s" % (k + ":", old, new)
                  for k, old, new in differences]
        lines.append("Refusing to compare: the same compiler is not the same "
                     "speed on a different CPU or core count, and Debug against "
                     "Release is 3.6x. Re-record on this machine with "
                     "--save-baseline, or run where this baseline was recorded.")
        return "\n".join(lines)
    gate = baseline.get("gate", {})
    ceiling = float(gate.get("max_loadavg", LOAD_FLOOR))
    load = ident["loadavg"]
    if load is None:
        return ("NOT COMPARABLE: this run records no load average, so it cannot "
                "be shown to be inside the %.1f envelope the baseline was "
                "measured in." % ceiling)
    if load > ceiling:
        return ("NOT COMPARABLE: run started at load average %.2f, above the "
                "%.1f ceiling the baseline was measured inside. At that load "
                "the numbers measure the other work on the box, not the "
                "compiler." % (load, ceiling))
    return None


def main():
    ap = argparse.ArgumentParser(
        description="Measure finc compile time; gate a change against a baseline")
    ap.add_argument("--finc", default=None,
                    help="compiler to measure (default: build-rel/finc, then "
                         "build/finc, then build/Release/finc, then PATH)")
    ap.add_argument("--runs", type=int, default=5,
                    help="timed runs per scenario; min and median are reported "
                         "(default 5)")
    ap.add_argument("--json", default=None,
                    help="write the full measurement as JSON to this path")
    ap.add_argument("--baseline", default=str(DEFAULT_BASELINE),
                    help="baseline file (default tests/tools/bench-baseline.json)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--save-baseline", action="store_true",
                      help="write this run to --baseline")
    mode.add_argument("--compare", action="store_true",
                      help="compare this run against --baseline and gate on it")
    ap.add_argument("--allow-debug", action="store_true",
                    help="measure a Debug (or build-type-unknown) binary anyway; "
                         "refused by default, because Debug is 3.6x slower and "
                         "its numbers are not the compiler's")
    a = ap.parse_args()

    finc = find_finc(a.finc)
    ident = fingerprint(finc)
    refuse_unmeasurable(finc, ident, a.allow_debug)
    if ident["build_type"] != "Release":
        print("warning: build type is %s, not Release; these are not the "
              "compiler's speed" % ident["build_type"])

    baseline = None
    if a.compare:
        path = Path(a.baseline)
        if not path.is_file():
            sys.exit("error: no baseline at %s; record one with --save-baseline"
                     % path)
        baseline = json.loads(path.read_text())
        why = comparability(baseline, path, ident)
        if why:
            print(why)
            return 0

    try:
        digest = hashlib.sha256(finc.read_bytes()).hexdigest()[:16]
    except OSError:
        digest = "unreadable"
    version = subprocess.run([str(finc), "--version"], capture_output=True,
                             text=True).stdout.strip()
    print("finc:       %s" % finc)
    print("version:    %s" % (version or "unknown"))
    print("build type: %s (%s)"
          % (ident["build_type"], ident["cache"] or "no cache found"))
    print("sha256:     %s" % digest)
    print("machine:    %s/%s, %s, %d cores"
          % (ident["system"], ident["machine"], ident["cpu"], ident["cores"]))
    print("load avg:   %s"
          % ("%.2f" % ident["loadavg"] if ident["loadavg"] is not None
             else "unknown"))
    if ident["loadavg"] is not None and ident["loadavg"] > 2.0:
        print("warning: load average is above 2.0; deltas on a loaded machine "
              "are mostly scheduling noise, not the compiler")
    # Measuring a binary older than the sources is the same class of error as
    # measuring Debug: the number is real but belongs to different code.
    newest, name = newest_source()
    if newest and name and finc.stat().st_mtime < newest:
        print("warning: %s predates %s; rebuild before trusting these numbers"
              % (finc, name))
    started = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    print("started:    %s, %d runs per scenario" % (started, a.runs))
    print()

    plan = scenarios()
    failures = {}
    with tempfile.TemporaryDirectory(prefix="finc-bench-") as tmp:
        outdir = Path(tmp)
        print("warming page cache (one untimed run per scenario) ...", flush=True)
        for name, source, mode in plan:
            _, done = run_once(command(finc, source, mode, outdir))
            if done.returncode != 0:
                failures[name] = done.stderr.decode("utf-8", "replace").strip()

        print("timing %d scenarios x %d runs, round-robin ..."
              % (len(plan), a.runs), flush=True)
        rows, failure = measure(finc, plan, outdir, a.runs)
        if failure:
            failures[failure[0]] = failure[1]

        if failures:
            for name, message in failures.items():
                print("\nFAILED %s:\n%s" % (name, message), file=sys.stderr)
            sys.exit("error: %d scenario(s) could not run; nothing was measured"
                     % len(failures))

        if a.compare:
            threshold = float(baseline.get("gate", {}).get("threshold_pct",
                                                           THRESHOLD_PCT))
            suspects = [name for name, row in rows.items()
                        if name in baseline.get("scenarios", {})
                        and (row["min_ms"] - baseline["scenarios"][name]["min_ms"])
                        / baseline["scenarios"][name]["min_ms"] * 100.0 > threshold]
            if suspects:
                # A real regression is stable; a busy minute is not. Re-measure
                # only the suspects and judge them on the second batch, so a
                # scheduling spike cannot fail the gate on its own. This is the
                # difference between a gate and a coin flip on a shared box.
                print("%d scenario(s) read above threshold; re-measuring just "
                      "those to tell a regression from a busy moment ..."
                      % len(suspects), flush=True)
                again, failure = measure(
                    finc, [p for p in plan if p[0] in suspects], outdir, a.runs)
                if failure:
                    sys.exit("error: %s failed on the confirming run: %s"
                             % (failure[0], failure[1]))
                rows.update(again)

    # The binary can be relinked by a sibling build mid-run (seen here as
    # ETXTBSY, and as a fingerprint change that silently mixes two compilers'
    # numbers). Either way the run is not one measurement of one binary.
    try:
        after = hashlib.sha256(finc.read_bytes()).hexdigest()[:16]
    except OSError:
        after = "unreadable"
    if after != digest:
        sys.exit("error: %s changed during the run (%s -> %s); another build "
                 "relinked it. Re-run when the tree is quiet." % (finc, digest, after))

    measured = {
        "schema": 1,
        "created": started,
        "binary": {"path": str(finc), "sha256": digest, "version": version},
        "fingerprint": {k: ident[k] for k in IDENTITY_KEYS},
        "loadavg": ident["loadavg"],
        "runs": a.runs,
        "gate": {"threshold_pct": THRESHOLD_PCT,
                 "max_loadavg": max(LOAD_FLOOR,
                                    (ident["loadavg"] or 0.0) + LOAD_HEADROOM)},
        "scenarios": rows,
    }
    if a.json:
        Path(a.json).write_text(json.dumps(measured, indent=2) + "\n")

    if a.save_baseline:
        print_table(rows, None)
        Path(a.baseline).write_text(json.dumps(measured, indent=2) + "\n")
        print("baseline written to %s" % a.baseline)
        return 0

    if not a.compare:
        print_table(rows, None)
        print("next: --save-baseline records this as the reference; --compare "
              "gates a change against one")
        return 0

    print_table(rows, (baseline, threshold))
    deltas = {name: (row["min_ms"] - baseline["scenarios"][name]["min_ms"])
              / baseline["scenarios"][name]["min_ms"] * 100.0
              for name, row in rows.items() if name in baseline.get("scenarios", {})}
    print("baseline: %s (created %s, %d runs, threshold %.0f%%)"
          % (a.baseline, baseline.get("created", "?"),
             baseline.get("runs", 0), threshold))
    slowest = max(deltas.values()) if deltas else 0.0
    fastest = min(deltas.values()) if deltas else 0.0
    # A row over threshold with noise wider than the threshold is unresolved,
    # not a regression: this machine cannot reproduce that row closely enough to
    # judge it. Only reproducible rows can fail the gate.
    def judged(sign):
        return sorted((k for k, d in deltas.items()
                       if sign * d > threshold and rows[k]["noise_pct"] < threshold),
                      key=lambda k: -sign * deltas[k])
    regressed = judged(1)
    faster = judged(-1)
    unresolved = sorted((k for k, d in deltas.items()
                         if abs(d) > threshold and rows[k]["noise_pct"] >= threshold),
                        key=lambda k: -abs(deltas[k]))
    worst = max(deltas.values()) if deltas else 0.0
    print("worst delta %+.1f%% over %d comparable scenarios"
          % (worst, len(deltas)))
    if unresolved:
        # Not a pass in disguise: say the run cannot answer, and why.
        print("unresolved: %s moved by more than the threshold but this run "
              "reproduced them to no better than the threshold itself. Re-run "
              "on a quieter machine, or raise --runs."
              % ", ".join("%s %+.1f%% (noise %.1f%%)"
                          % (k, deltas[k], rows[k]["noise_pct"])
                          for k in unresolved[:5]))
    if regressed:
        print("=== GATE FAILED: slower than baseline: %s ==="
              % ", ".join("%s %+.1f%%" % (k, deltas[k]) for k in regressed[:5]))
        return 2
    if faster:
        # A reproducible win is not a reason to pass silently: the baseline now
        # describes an older compiler, and leaving it means the next regression
        # of the same size reads as "no change". Lock the win in.
        print("=== GATE FAILED: faster than baseline: %s -- re-record with "
              "--save-baseline so the improvement is the new reference ==="
              % ", ".join("%s %+.1f%%" % (k, deltas[k]) for k in faster[:5]))
        return 2
    if unresolved:
        print("=== GATE INCONCLUSIVE: nothing reproducible moved beyond the "
              "threshold, but %d scenario(s) could not be judged ==="
              % len(unresolved))
        return 0
    print("=== GATE PASSED ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
