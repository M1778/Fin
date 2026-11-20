// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

#pragma once

// The semver is derived, never written down here. `FINC_VERSION` is defined by
// CMakeLists.txt from ${PROJECT_VERSION} — `project(FinCompiler VERSION ...)`
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
// Contract history:
//   1 -> 2: `-o <path>` honours executables (self-hosting bootstrap).
//   2 -> 3: `-o` (without `-c`) refused together with `--diagnostics=json`,
//           exit 2 (issue #73): an executable build runs an external C driver
//           whose own output reaches the JSON stream raw, so the argv pattern
//           a contract-2 caller could issue no longer means "link it".
inline constexpr int kFincContractVersion = 3;

}
