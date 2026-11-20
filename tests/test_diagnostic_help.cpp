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

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "diagnostics/DiagnosticEngine.hpp"

// `finc --explain [<code>|list]` (docs/diagnostics.md). The catalogue the
// binary answers from is the same table that assigns codes when a diagnostic is
// emitted, so every code here is one the compiler can actually produce. Most
// tests read it back through the CLI, because the flag is the contract rather
// than the C++ symbol; the example check reads the table directly so a second,
// untested copy of the examples cannot grow.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class TempFin {
public:
    explicit TempFin(const std::string& contents, const std::string& tag = "t") {
        path_ = uniqueTempPath("fin_explain_" + tag, ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~TempFin() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(start, nl - start);
        if (!line.empty()) out.push_back(line);
        start = nl + 1;
    }
    return out;
}

// The code that prefixes an inventory line, e.g. "E0201" in
// "E0201  error  Type mismatch: ...".
std::string codeOf(const std::string& line) {
    const size_t end = line.find_first_of(" \t");
    return line.substr(0, end == std::string::npos ? line.size() : end);
}

bool isCode(const std::string& s) {
    if (s.size() != 5 || s[0] != 'E') return false;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// `finc --explain list`, split into inventory lines.
std::vector<std::string> inventory() {
    auto r = runFinc({"--explain", "list"});
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_EQ(r.err, "") << "--explain is informational: nothing goes to stderr";
    return splitLines(r.out);
}

std::string docText() {
    const fs::path doc = fs::path(testsDir()).parent_path() / "docs" / "diagnostics.md";
    std::ifstream f(doc, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST(DiagnosticHelp, KnownCodePrintsItsExplanationToStdout) {
    auto r = runFinc({"--explain", "E0201"});
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_EQ(r.err, "") << "an explanation is asked-for output: it goes to stdout only";

    const std::string out = stripAnsi(r.out);
    EXPECT_NE(out.find("E0201"), std::string::npos) << out;
    EXPECT_NE(out.find("Type mismatch"), std::string::npos)
        << "the explanation names the message family it explains:\n" << out;
    EXPECT_NE(out.find("error"), std::string::npos)
        << "the explanation states the severity:\n" << out;
    EXPECT_NE(out.find("Example:"), std::string::npos)
        << "the explanation carries a triggering example:\n" << out;
}

TEST(DiagnosticHelp, ExplainNeedsNeitherASourceFileNorAnOutputPath) {
    // Exactly what was run above: no positional file, no -o, no -c.
    auto r = runFinc({"--explain", "E0101"});
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_FALSE(r.out.empty());
}

TEST(DiagnosticHelp, EveryCodeInTheInventoryIsReachable) {
    const auto lines = inventory();
    ASSERT_FALSE(lines.empty()) << "--explain list printed nothing";
    for (const auto& line : lines) {
        const std::string code = codeOf(line);
        ASSERT_TRUE(isCode(code)) << "not a code: " << line;
        SCOPED_TRACE(code);
        auto r = runFinc({"--explain", code});
        EXPECT_EQ(r.exitCode, 0) << code << " is listed but cannot be explained:\n" << r.err;
        EXPECT_NE(r.out.find(code), std::string::npos)
            << "the explanation must name its own code:\n" << r.out;
        EXPECT_GT(r.out.size(), 80u) << "the explanation is only a stub:\n" << r.out;
    }
}

TEST(DiagnosticHelp, ListIsSortedAndNonEmpty) {
    const auto lines = inventory();
    ASSERT_GE(lines.size(), 1u);

    std::vector<std::string> codes;
    for (const auto& line : lines) {
        const std::string code = codeOf(line);
        ASSERT_TRUE(isCode(code)) << "not an inventory line: " << line;
        codes.push_back(code);
    }
    EXPECT_TRUE(std::is_sorted(codes.begin(), codes.end()))
        << "the inventory is sorted by code";
    EXPECT_EQ(std::adjacent_find(codes.begin(), codes.end()), codes.end())
        << "a code is listed twice";
    // Each line is `code  severity  one-line meaning`, so a reader can scan it.
    for (const auto& line : lines) {
        EXPECT_NE(line.find("error"), std::string::npos)
            << "no severity on an inventory line: " << line;
        EXPECT_GT(line.size(), codeOf(line).size() + 8)
            << "no one-line meaning on an inventory line: " << line;
    }
}

TEST(DiagnosticHelp, EveryListedCodeIsDocumentedInTheCatalogue) {
    const std::string doc = docText();
    ASSERT_FALSE(doc.empty()) << "docs/diagnostics.md is missing";
    for (const auto& line : inventory()) {
        const std::string code = codeOf(line);
        EXPECT_NE(doc.find(code), std::string::npos)
            << code << " is emitted and explained but absent from docs/diagnostics.md";
        // The page quotes the same example the C++ table compiles, so the prose
        // cannot drift from the program the suite proves still triggers the code.
        auto r = runFinc({"--explain", code});
        const std::string marker = "Example:\n\n";
        const size_t at = r.out.find(marker);
        ASSERT_NE(at, std::string::npos) << code << " has no example:\n" << r.out;
        const size_t end = r.out.find("\n\n", at + marker.size());
        const std::string example = r.out.substr(
            at + marker.size(), end == std::string::npos ? std::string::npos
                                                        : end - at - marker.size());
        EXPECT_NE(doc.find(example), std::string::npos)
            << code << "'s example is not the one docs/diagnostics.md quotes:\n" << example;
    }
}

TEST(DiagnosticHelp, TheCatalogueExamplesTriggerTheirCodes) {
    // A catalogue example that does not reproduce its own code is an invented
    // rule. Read from the same table `--explain` answers from, so the examples
    // cannot drift into a second, untested list. Compiled for real, in JSON so
    // the code is readable off the stream; `-c` is what reaches lowering, which
    // the E0101 example needs and the E0201 example fails before.
    for (const auto& doc : fin::diagnosticCatalogue()) {
        SCOPED_TRACE(doc.code);
        if (doc.example.empty()) {
            ADD_FAILURE() << doc.code << " has no example";
            continue;
        }
#ifndef FIN_TESTS_HAVE_BACKEND
        if (doc.code == "E0101") {
            GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF: nothing is lowered";
        }
#endif
        TempFin f(doc.example, doc.code);
        const std::string obj = uniqueTempPath("fin_explain_code", ".o");
        auto r = runFinc({f.str(), "-c", "-o", obj, "--diagnostics=json"});
        std::error_code ec;
        fs::remove(obj, ec);
        EXPECT_EQ(r.exitCode, 1) << stripAnsi(r.err);
        EXPECT_NE(r.err.find(std::string("\"code\":\"") + doc.code + "\""), std::string::npos)
            << "the catalogue example does not reproduce " << doc.code << ":\n" << r.err;
    }
}

TEST(DiagnosticHelp, UnknownCodeExitsTwoAndNamesIt) {
    auto r = runFinc({"--explain", "E9999"});
    EXPECT_EQ(r.exitCode, 2) << stripAnsi(r.err);
    EXPECT_EQ(r.out, "") << "stdout is reserved on an error path";
    const std::string err = stripAnsi(r.err);
    EXPECT_NE(err.find("E9999"), std::string::npos)
        << "a bad code must be named:\n" << err;
    EXPECT_NE(err.find("--explain"), std::string::npos)
        << "the help must point at the way to list the real codes:\n" << err;
}

TEST(DiagnosticHelp, UnknownCodeIsAJsonlCommandLineErrorWithASummary) {
    auto r = runFinc({"--diagnostics=json", "--explain", "E9999"});
    EXPECT_EQ(r.exitCode, 2) << r.err;
    EXPECT_EQ(r.out, "") << "stdout is reserved on an error path";

    const auto lines = splitLines(r.err);
    ASSERT_GE(lines.size(), 2u) << r.err;
    for (const auto& line : lines) {
        EXPECT_EQ(line.front(), '{') << line;
        EXPECT_EQ(line.back(), '}') << line;
    }
    EXPECT_NE(lines.front().find("\"kind\":\"diagnostic\""), std::string::npos)
        << lines.front();
    EXPECT_NE(lines.front().find("E9999"), std::string::npos) << lines.front();
    EXPECT_NE(lines.front().find("\"severity\":\"error\""), std::string::npos)
        << lines.front();
    EXPECT_NE(lines.front().find("\"file\":null"), std::string::npos)
        << "an unknown code is about the invocation, not a place in a file:\n"
        << lines.front();
    EXPECT_NE(lines.back().find("\"kind\":\"summary\""), std::string::npos)
        << lines.back();
    EXPECT_NE(lines.back().find("\"exitCode\":2"), std::string::npos) << lines.back();
}

TEST(DiagnosticHelp, AMissingOperandIsAUsageError) {
    auto r = runFinc({"--explain"});
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_EQ(r.out, "");
    EXPECT_NE(stripAnsi(r.err).find("--explain"), std::string::npos) << stripAnsi(r.err);
}

TEST(DiagnosticHelp, ExplainIsAdvertisedInHelp) {
    auto r = runFinc({"--help"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_NE(r.out.find("--explain"), std::string::npos)
        << "--help must advertise the flag it accepts";
}

// --- The registry's rules must partition the message space -------------------
//
// The emitter takes the FIRST rule that matches, so an overlap is not a cosmetic
// defect: it silently hands one family's messages another family's code, and the
// losing row becomes dead for that shape. These tests use the real predicates
// from `diagnosticRules()` rather than copies, so a rule added without these
// properties fails here instead of shipping a code that lies.

namespace {

bool ruleMatches(const fin::DiagnosticRule& rule, const std::string& message) {
    if (!rule.prefix.empty() && message.compare(0, rule.prefix.size(), rule.prefix) != 0) {
        return false;
    }
    if (!rule.contains.empty() &&
        message.find(rule.contains) == std::string::npos) {
        return false;
    }
    return true;
}

// One representative message per rule: its own literal family, rendered the way
// the compiler would. A rule with an empty prefix and an empty contains would
// match everything, so it is excluded by construction here.
std::string representative(const fin::DiagnosticRule& rule) {
    std::string out = rule.prefix;
    if (out.empty() || out.back() != ' ') out += "value";
    if (!rule.contains.empty()) out += rule.contains;
    return out;
}

} // namespace

TEST(DiagnosticHelp, NoTwoCodesMatchTheSameMessage) {
    // The decisive check: every pair of rules, against a message each of them
    // matches on its own. If a message matches both, one of the two codes can
    // never be the answer for it, and which one loses depends on row order.
    const auto& rules = fin::diagnosticRules();
    ASSERT_GE(rules.size(), 2u);
    for (size_t i = 0; i < rules.size(); ++i) {
        const std::string message = representative(rules[i]);
        for (size_t j = 0; j < rules.size(); ++j) {
            if (i == j) continue;
            SCOPED_TRACE(rules[i].code + " <- \"" + message + "\" also matches " +
                         rules[j].code);
            EXPECT_FALSE(ruleMatches(rules[j], message))
                << "rule " << rules[j].code << " steals a message from "
                << rules[i].code << ": " << message;
        }
    }
}

TEST(DiagnosticHelp, EveryRegisteredPrefixIsUniqueOrDisjoint) {
    // A shared prefix is only allowed when the `contains` halves separate the
    // families, which is exactly what NoTwoCodesMatchTheSameMessage proves with
    // a message. What this test adds is the pair-level statement: two rules with
    // the same prefix AND a `contains` the other lacks would overlap on the
    // shorter rule's messages, so such a pair must be flagged here by name.
    const auto& rules = fin::diagnosticRules();
    for (size_t i = 0; i < rules.size(); ++i) {
        for (size_t j = i + 1; j < rules.size(); ++j) {
            if (rules[i].prefix != rules[j].prefix) continue;
            SCOPED_TRACE(rules[i].code + " and " + rules[j].code +
                         " share prefix \"" + rules[i].prefix + "\"");
            // Same prefix and both `contains` empty: identical rules.
            EXPECT_FALSE(rules[i].contains.empty() && rules[j].contains.empty())
                << "two identical match rules";
            // Same prefix, one contains empty: the empty one matches everything
            // the other does, so the families are not distinguishable.
            EXPECT_FALSE(rules[i].contains.empty() != rules[j].contains.empty())
                << "one rule's `contains` cannot separate it from a rule with none";
        }
    }
}

TEST(DiagnosticHelp, EveryRuleNeedsAPredicateAndARegisteredCode) {
    const auto& rules = fin::diagnosticRules();
    ASSERT_FALSE(rules.empty());
    for (const auto& rule : rules) {
        SCOPED_TRACE(rule.code);
        // A row with neither field matches every message, so it would claim the
        // whole stream. That is never a family.
        EXPECT_FALSE(rule.prefix.empty() && rule.contains.empty())
            << "a rule with no predicate matches every diagnostic";
        EXPECT_NE(fin::findDiagnosticDoc(rule.code), nullptr)
            << "a match rule names a code --explain cannot explain";
    }
}

