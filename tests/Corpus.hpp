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
#include <string>
#include <vector>

namespace fin::testing {

// An expectation is the `//@` line in a sample that says what the compiler
// should do with it. Authority lives here rather than in the file's label, so
// one construct inside a normative sample can be excused and one inside an
// aspirational sample can be held to account (ADR 0008).
enum class ExpectationKind {
    Ok,             // `//@ ok`
    Error,          // `//@ error <line>:<col> "<msg>"`
    Unimplemented,  // `//@ unimplemented "<reason>"`
};

struct Expectation {
    ExpectationKind kind = ExpectationKind::Ok;
    int line = 0;        // Error only
    int column = 0;      // Error only
    std::string text;    // message for Error, reason for Unimplemented
    int sourceLine = 0;  // where in the sample this expectation was written
};

// The file's normative-or-aspirational label. Advisory: a reading aid and a
// default, never the thing that decides whether the compiler is at fault.
enum class Authority { Unlabelled, Normative, Aspirational };

struct SampleAnnotation {
    std::string path;
    Authority authority = Authority::Unlabelled;
    std::vector<Expectation> expectations;
    // Non-empty when the `//@` lines could not be understood. A sample with no
    // expectation, or with an expectation the harness cannot parse, is a
    // harness failure and never a skip.
    std::string error;
};

// Parses the `//@` lines out of a sample's text.
SampleAnnotation parseAnnotation(const std::string& path, const std::string& source);

// Every `.fin` file under tests/samples, sorted. Absolute paths built from a
// compile-time definition, so the result does not depend on the working
// directory the test binary happens to be launched from.
const std::vector<std::string>& sampleFiles();

// `.fin` files in the tests tree that are deliberately not samples. The list is
// explicit so that a scratch file cannot be silently globbed into the
// specification (ADR 0008).
const std::vector<std::string>& scratchFinFiles();

// Every `.fin` file anywhere under tests/, sorted.
std::vector<std::string> allFinFilesInTestTree();

std::string readWholeFile(const std::string& path);
// Normalize Windows text-stream line endings without changing source-file reads.
std::string readProcessOutput(const std::string& path);

// The name gtest shows for a parameterised sample: "stdlib/stdio.fin" becomes
// "stdlib_stdio".
std::string testNameForSample(const std::string& path);

// Result of running the finc binary once.
struct FincRun {
    int exitCode = -1;
    std::string out;   // stdout — reserved, expected empty
    std::string err;   // stderr — diagnostics
};

// Runs the finc binary built alongside this test binary.
FincRun runFinc(const std::vector<std::string>& args,
                const std::vector<std::pair<std::string, std::string>>& env = {});

std::string fincBinary();
std::string samplesDir();
std::string testsDir();

// Strips ANSI escapes so an assertion can read the human renderer.
std::string stripAnsi(const std::string& s);

// A path under the system temp directory that no other test, no concurrently
// running test *process*, and no leftover from a crashed run can collide with.
// Nothing is created on disk; the caller decides whether it becomes a file or a
// directory.
//
// This exists because `ctest -j` runs every test in its own process, so a
// per-process `static int counter` — which four separate temp-file helpers in
// this suite had each grown independently, as `<prefix>_<counter>` — restarts at
// 0 in every worker and hands two concurrent tests the same path, whereupon each
// one's destructor deletes the other's file. It produced eleven failures that
// all passed on rerun: the worst failure mode available, because a suite that
// goes red for a reason unrelated to the change under test teaches everyone to
// rerun it until it is green. Route every temporary path through here.
//
// Which part of the name does the work was measured rather than assumed, by
// rebuilding this function three ways and running `ctest -j8` three times each:
//
//   <prefix>_<counter>            27, 30 and 31 failures — the count varies per
//                                 run, which is the signature to recognise
//   <prefix>_<pid>_<counter>      green, green, green
//   <prefix>_<test>_<counter>     green, green, green
//
// So *either* discriminator closes the `-j` hole and neither is load-bearing
// over the other; both are kept because they cover different second cases. The
// test name is what makes a file leaked by a crash say which test leaked it. The
// pid is what makes two *concurrent `ctest` invocations* safe — which is a real
// case in this repository, where more than one agent runs the suite against one
// build directory, and which the test name alone does not cover.
std::string uniqueTempPath(const std::string& prefix, const std::string& suffix = "");

}
