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

"""Compare a run_bench.py result file against a checked-in baseline.

    python3 bench/run_bench.py --repeat 3 --json /tmp/bench.json
    python3 bench/check_bench.py --results /tmp/bench.json

Exit status:
  0  every gated metric is within tolerance, OR no baseline matches this
     machine (incomparable is SKIPPED, never "faster" and never a failure)
  2  a gated metric regressed beyond the baseline's tolerance
  1  the result file is missing a metric the baseline expects

The baseline carries the machine fingerprint it was measured on, because
timings are a property of the machine as much as of the compiler: a run on a
different CPU compares to nothing. See docs/PERFORMANCE_NOTES.md.
"""
import argparse
import glob
import json
import os
import sys

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
BASELINE_DIR = os.path.join(BENCH_DIR, "baselines")

# Fields that make two machines comparable. cpu is the model string from
# /proc/cpuinfo, so the same architecture at a different clock is still
# "comparable" only by the tolerance -- documented as a known limit.
MATCH_KEYS = ("family", "arch", "cpu", "virt")


def fingerprint(env):
    return {k: env.get(k) for k in MATCH_KEYS}


def load_baselines():
    out = []
    for path in sorted(glob.glob(os.path.join(BASELINE_DIR, "*.json"))):
        with open(path) as f:
            out.append((path, json.load(f)))
    return out


def metric_ok(m, stem, phase, lang):
    try:
        return m["metrics"][stem][phase][lang]["median_s"] is not None
    except (KeyError, TypeError):
        return False


def main():
    ap = argparse.ArgumentParser(description="Gate a benchmark run against a baseline")
    ap.add_argument("--results", required=True)
    ap.add_argument("--baseline", default=None, help="explicit baseline file")
    ap.add_argument("--max-load", type=float, default=None,
                    help="override the load-average ceiling; by default the "
                         "baseline's own gate.max_loadavg is used, which is the "
                         "busiest machine state its numbers were measured in")
    a = ap.parse_args()

    with open(a.results) as f:
        run = json.load(f)
    env = run.get("environment", {})
    mine = fingerprint(env)
    print("this machine: %s" % json.dumps(mine, sort_keys=True))

    candidates = ([(a.baseline, json.load(open(a.baseline)))] if a.baseline
                  else load_baselines())
    if not candidates:
        print("SKIP: no baseline under bench/baselines/")
        return 0

    match = None
    for path, base in candidates:
        if fingerprint(base.get("machine", {})) == mine:
            match = (path, base)
            break
        print("  not comparable: %s (measured on %s)"
              % (os.path.basename(path),
                 json.dumps(fingerprint(base.get("machine", {})), sort_keys=True)))

    if match is None:
        # Deliberately not a failure: a machine without a baseline says nothing
        # about the compiler, and calling it a regression would be invented.
        print("SKIP: no baseline for this machine. Record one with "
              "bench/run_bench.py --repeat 3 and add it under bench/baselines/.")
        return 0

    path, base = match
    built = base.get("machine", {}).get("finc_build_type")
    mine_built = env.get("finc_build_type")
    if built and built not in (None, "unknown") and mine_built != built:
        print("SKIP: baseline was measured with a %s finc, this run used %s. "
              "Build types are not interchangeable (measured 1.02x-1.11x on "
              "compile rows), so this run is not comparable."
              % (built, mine_built or "a finc whose build type is unknown"))
        return 0
    tol = float(base["gate"]["tolerance_pct"])
    print("baseline: %s (git %s, %s, tolerance %.0f%%)"
          % (os.path.basename(path), base.get("git_head", "?")[:12],
             base.get("created", "?"), tol))

    # A result measured on a loaded box is not a measurement of the compiler.
    # Observed here: the same compiler on the same WSL2 machine read 6-11%
    # slower across the whole pipeline while another agent's build held load at
    # 3-15, and pooled deviations reached +16.5%. Even at load 1.96 an unchanged
    # compiler read +14.8% on a compile row. The tolerance is only valid inside
    # the load envelope the baseline was measured in, so anything busier is not
    # judged; the ceiling comes from the baseline, not from a guess here.
    ceiling = a.max_load
    if ceiling is None:
        ceiling = float(base.get("gate", {}).get("max_loadavg", 1.5))
    load = env.get("loadavg")
    if load is None:
        # Without a recorded load the envelope cannot be checked. Measured here:
        # runs taken before this field existed (load ~2-3) read +15-25% against
        # the same compiler's baseline, so judging one would invent a regression.
        print("SKIP: this result records no loadavg, so it cannot be shown to be "
              "inside the %.2f load envelope the baseline was measured in. "
              "Re-run with a bench/run_bench.py that records it." % ceiling)
        return 0
    if load > ceiling:
        print("SKIP: run started at load average %.2f, above the %.2f ceiling "
              "the baseline was measured inside; timings outside it are not "
              "comparable" % (load, ceiling))
        return 0

    if env.get("finc") != base.get("machine", {}).get("finc"):
        print("note: compiler differs from the baseline (%s vs %s); a delta can be "
              "the compiler change rather than machine drift"
              % (env.get("finc"), base.get("machine", {}).get("finc")))

    failed, worst = False, 0.0
    for entry in base["gate"]["metrics"]:
        stem, phase, lang = entry.split("/")
        if not metric_ok(run, stem, phase, lang):
            print("FAIL: result file has no %s" % entry)
            return 1
        got = run["metrics"][stem][phase][lang]["median_s"]
        want = base["metrics"][stem][phase][lang]["median_s"]
        delta = (got - want) / want * 100.0
        worst = max(worst, delta)
        verdict = "REGRESSED" if delta > tol else "ok"
        print("  %-9s %-8s %-3s %.4fs vs %.4fs = %+6.1f%%  %s"
              % (stem, phase, lang, got, want, delta, verdict))
        if delta > tol:
            failed = True

    print("worst delta %+.1f%% against a %.0f%% tolerance" % (worst, tol))
    if failed:
        print("=== GATE FAILED ===")
        return 2
    print("=== GATE PASSED ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
