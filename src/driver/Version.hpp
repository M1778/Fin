#pragma once

// The semver is derived, never written down here. `FINC_VERSION` is defined by
// CMakeLists.txt from ${PROJECT_VERSION} — `project(FinCompiler VERSION 0.4.0)`
// on line 2 — which is the same value the release workflow reads when it names
// archives `finc-<semver>-<triple>` and the value conanfile.py's set_version()
// parses. One place, three consumers.
//
// There is deliberately no fallback literal. A fallback is exactly the drift
// being removed: a literal here would silently disagree with the project version
// the moment either moved, which is the defect `finn` fixed in itself at
// registry.rs:53 by reading env!("CARGO_PKG_VERSION"). A missing definition has
// to break the build instead.
#ifndef FINC_VERSION
#  error "FINC_VERSION is undefined. CMakeLists.txt defines it from ${PROJECT_VERSION} as PUBLIC on fin_core, so anything including this header must link fin_core. There is no fallback literal on purpose: a fallback is the version drift this removes."
#endif

namespace fin {

// `finc --version` prints `finc <semver> (contract <int>)`. The semver is the
// compiler's own; the contract integer is the version of the machine contract
// (ADR 0009) — argv grammar, exit codes, stream discipline and the
// `--diagnostics=json` schema. `finn` branches on the contract integer, not on
// the semver, because a compiler patch release must not look like a protocol
// change. They move independently, so the contract integer is a literal with no
// other claimant while the semver is derived.
//
// These are `kFincVersion`, not `FINC_VERSION`: the macro owns that spelling and
// a same-named constant would be rewritten by the preprocessor.
inline constexpr const char* kFincVersion = FINC_VERSION;
inline constexpr int kFincContractVersion = 1;

}
