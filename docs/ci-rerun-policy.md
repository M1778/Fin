# CI rerun policy: transient infra vs repo-side failure

Probe date: 2026-10-06 (runs 37351730614–37487531090, PR #9 run 37485563495).
Scope: fast failures in `setup-toolchain` / Conan / Configure (jobs dying in
~1–6 min). Downstream Build/Test/Bootstrap product failures are out of scope.

## Per-platform diagnosis

| Platform | Verdict |
|---|---|
| windows-arm64 | Transient infra (once): GitHub release assets returned HTTP 500 x4 on `clang+llvm-22.1.8-aarch64-pc-windows-msvc.tar.xz`, exhausting `curl --retry 3` in ~8s (run 37485563495, setup dead in 51s). URL and `aarch64` arch mapping verified live (HTTP 200); the same step passed in 4+ other runs. |
| windows-x86_64 | Setup green in every sampled run; failures are product Build errors (`test_stage_agreement.cpp` C2466). |
| linux x86_64 / arm64 | Setup green in every sampled run (apt.llvm.org, Conan, Configure all pass). The ~3 min "no finc binary" line is the Bootstrap step echoing its own guard script; the real error is the stage2/stage3 `cmp` fixed-point divergence (product). No ENOSPC, no apt 404. |
| macOS arm64 / x86_64 | Setup green in every sampled run (`brew install llvm@22` works); failures are product Test failures in `test_codegen.cpp`. |
| build.sh job | Setup green; intermittent failures are ctest product failures (exit 8, one `Soundness_Codegen` test), not setup. |

Ruled out as repo-side bugs: dead LLVM URL (alive), arch mapping
(`aarch64` asset exists upstream), Conan cache key (misses self-heal via
`--build=missing` + fresh `profile detect`), disk exhaustion (no evidence).

## What changed repo-side

`.github/actions/setup-toolchain/action.yml`: retry the network operations
that have no second chance today -- Windows LLVM archive (`--retry 10`,
10s backoff, capped at 10 min), winflexbison (5x pwsh loop), apt update /
install and brew install (5x `retry` loops). Persistent failures still fail
the step: no `|| true`, no platform removed, gates unchanged.

## Rerun policy

1. A job that dies in `setup-toolchain` / Conan / Configure: re-run failed
   jobs once. A green rerun confirms transient infra; do not "fix" anything.
2. The same setup step fails twice in a row (rerun + next run): treat as
   repo-side -- open an issue with the step log instead of rerunning again.
3. Never mask setup with `|| true`, `continue-on-error`, or by deleting the
   platform row: a skipped toolchain is how backend-less builds go quiet.
