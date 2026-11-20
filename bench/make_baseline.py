#!/usr/bin/env python3
"""Write bench/baselines/<name>.json from one or more run_bench.py result files.

    python3 bench/make_baseline.py --name wsl2-i5-14400f \
        --label "WSL2 on Intel i5-14400F" \
        --inputs /home/M1778/fin-tmp/rel1.json ... /home/M1778/fin-tmp/rel6.json

Each metric is the median of the per-invocation gated metrics (itself the min of
the pass medians), so the baseline is the same statistic the gate compares
against. Every number comes from the input files; nothing is typed by hand.

Schemas this produces, read by bench/check_bench.py:

  machine              fingerprint; compared key by key (family, arch, cpu, virt)
  created / git_head   when and from which commit the numbers were measured
  method               how each metric was reduced; must match the gate's protocol
  gate.tolerance_pct   a Fin metric may be this much slower than baseline
  gate.metrics         "stem/phase/lang" rows the gate fails on
  metrics              stem -> phase -> lang -> {median_s, spread_pct, samples}
"""
import argparse
import json
import os
import statistics

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
GATED = [("%s/%s/Fin" % (stem, phase))
         for stem in ("fib", "qsort_big", "mmul_big")
         for phase in ("compile", "runtime")]
LANGS = ("C", "C++", "Fin")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="file stem under bench/baselines/")
    ap.add_argument("--label", required=True, help="one-line machine description")
    ap.add_argument("--inputs", nargs="+", required=True)
    ap.add_argument("--tolerance", type=float, default=15.0)
    ap.add_argument("--max-load", type=float, default=None,
                    help="1-minute load average ceiling; default: the busiest "
                         "load among --inputs, rounded up")
    ap.add_argument("--note", default="")
    a = ap.parse_args()

    runs = [json.load(open(p)) for p in a.inputs]
    env = runs[0]["environment"]
    fingerprints = {json.dumps({k: r["environment"].get(k) for k in
                                ("family", "arch", "cpu", "virt")}, sort_keys=True)
                    for r in runs}
    if len(fingerprints) != 1:
        raise SystemExit("inputs are from different machines: %s" % fingerprints)

    metrics = {}
    for stem in ("fib", "qsort_big", "mmul_big"):
        metrics[stem] = {}
        for phase in ("compile", "runtime"):
            metrics[stem][phase] = {}
            for lang in LANGS:
                samples = [r["metrics"][stem][phase][lang]["median_s"] for r in runs]
                if any(s is None for s in samples):
                    continue
                med = statistics.median(samples)
                metrics[stem][phase][lang] = {
                    "median_s": round(med, 6),
                    "spread_pct": round((max(samples) - min(samples)) / med * 100.0, 1),
                    "samples": len(samples),
                }

    loads = [r["environment"].get("loadavg") for r in runs]
    loads = [x for x in loads if x is not None]
    max_load = round(max(loads) + 0.05, 2) if loads else None

    doc = {
        "schema": 1,
        "label": a.label,
        "created": runs[0].get("created", ""),
        "git_head": runs[0].get("git_head", ""),
        "machine": {k: env.get(k) for k in ("family", "arch", "cpu", "virt",
                                            "cpu_count", "finc",
                                            "finc_build_type", "finc_mtime",
                                            "gcc", "llvm_pin")},
        "method": {
            "protocol": "run_bench.py --repeat 3 --compile-runs 5 --run-runs 3",
            "gated_metric": "min of the 3 pass medians; each pass median is the "
                            "median of its own runs",
            "baseline_value": "median of the gated metric across %d invocations"
                              % len(runs),
            "spread_pct": "max-min of those gated metrics, as a percentage of the "
                          "median (observed variance, not a bound)",
        },
        "gate": {
            "tolerance_pct": a.tolerance,
            "max_loadavg": a.max_load if a.max_load is not None else max_load,
            "metrics": GATED,
            "note": "only Fin rows gate: a C/C++ row moves with gcc, not with finc, "
                    "and is recorded as the same-machine control",
        },
        "metrics": metrics,
        "note": a.note,
    }
    out = os.path.join(BENCH_DIR, "baselines", a.name + ".json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    print("wrote %s" % out)
    for stem, phases in metrics.items():
        for phase, langs in phases.items():
            for lang, m in langs.items():
                print("  %-9s %-8s %-4s %.5fs  spread %.1f%%"
                      % (stem, phase, lang, m["median_s"], m["spread_pct"]))


if __name__ == "__main__":
    main()
