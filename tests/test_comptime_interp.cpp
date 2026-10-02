#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/FunctionDecl.hpp"
#include "ast/decls/Program.hpp"
#include "ast/decls/TypeDef.hpp"
#include "ast/stmts/Statement.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/ComptimeInterp.hpp"

using namespace fin::testing;

namespace {

fin::SpecialDeclaration* findSpecial(fin::Program& program, const std::string& name) {
    for (auto& stmt : program.statements) {
        auto* decl = dynamic_cast<fin::SpecialDeclaration*>(stmt.get());
        if (decl && decl->name == name) return decl;
    }
    return nullptr;
}

struct Parsed {
    std::unique_ptr<fin::DiagnosticEngine> diag;
    ParseResult result;
};

Parsed parse(const std::string& code) {
    Parsed p;
    p.diag = std::make_unique<fin::DiagnosticEngine>("", "<test>");
    p.diag->setColorMode(fin::ColorMode::Never);
    p.result = parseSource(code, *p.diag);
    return p;
}

class ProvSrc {
public:
    explicit ProvSrc(const std::string& contents) {
        path_ = uniqueTempPath("fin_comptime_prov", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~ProvSrc() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    std::string str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

FincRun provCompile(const std::string& code) {
    ProvSrc s(code);
    return runFinc({s.str()});
}

std::string provMessages(const std::string& stripped) {
    std::string out;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) out.append(stripped, i, eol - i).append("\n");
        i = eol + 1;
    }
    return out;
}

} // namespace

// Slice 1: the value model threads literals + lets to the return.
TEST(ComptimeValue, LetThreadsLiteralToReturn) {
    auto p = parse("@special h() <int> {\n  let x <int> = 42;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    ASSERT_NE(h->body, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.kind, fin::comptime::ValueKind::Int);
    EXPECT_EQ(r.value.text, "42");
}

// Slice 1: parameters bind — a let may alias a bound param.
TEST(ComptimeValue, ParamsBindThroughLets) {
    auto p = parse("@special h(name: string) <string> {\n  let n <string> = name;\n  return n;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("name", fin::comptime::Value::makeString("hi"));
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.kind, fin::comptime::ValueKind::String);
    EXPECT_EQ(r.value.text, "hi");
}

// Slice 1: calls thread — a straight-line helper runs with bound args.
TEST(ComptimeValue, CallsThreadThroughHelpers) {
    auto p = parse(
        "fun id(v: int) <int> {\n  return v;\n}\n"
        "@special h() <int> {\n  let y <int> = id(7);\n  return y;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.kind, fin::comptime::ValueKind::Int);
    EXPECT_EQ(r.value.text, "7");
}

// S2 (branches, ADR 0006 amendment): `if`/`else` over a comptime-known
// bool takes its arm. Both arms must typecheck — the analyzer walks both —
// but only the taken arm evaluates (analyze-but-don't-emit): untaken lets
// never bind and untaken blame never fires.
TEST(ComptimeBranches, IfElseTakesKnownArm) {
    auto p = parse(
        "@special h(c: bool) <int> {\n"
        "  if (c) {\n"
        "    return 1;\n"
        "  } else {\n"
        "    return 2;\n"
        "  }\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    {
        fin::comptime::Interpreter interp(*p.result.ast);
        fin::comptime::Env env;
        env.bind("c", fin::comptime::Value::makeBool("true"));
        auto r = interp.evaluateBody(*h->body, env);
        ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
        EXPECT_EQ(r.value.text, "1");
    }
    {
        fin::comptime::Interpreter interp(*p.result.ast);
        fin::comptime::Env env;
        env.bind("c", fin::comptime::Value::makeBool("false"));
        auto r = interp.evaluateBody(*h->body, env);
        ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
        EXPECT_EQ(r.value.text, "2");
    }
}

TEST(ComptimeBranches, IfWithoutElseFallsThrough) {
    auto p = parse(
        "@special h(c: bool) <int> {\n"
        "  if (c) {\n"
        "    return 1;\n"
        "  }\n"
        "  return 2;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("c", fin::comptime::Value::makeBool("false"));
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "2");
}

TEST(ComptimeBranches, UntakenBlameDoesNotFire) {
    auto p = parse(
        "@special h() <int> {\n"
        "  if (false) {\n"
        "    blame \"boom\";\n"
        "  }\n"
        "  return 1;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "1");
}

TEST(ComptimeBranches, TakenLetsBindPastTheArm) {
    auto p = parse(
        "@special h() <int> {\n"
        "  if (true) {\n"
        "    let x <int> = 1;\n"
        "  }\n"
        "  return x;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "1");
}

TEST(ComptimeBranches, UntakenLetsDoNotLeak) {
    auto p = parse(
        "@special h() <int> {\n"
        "  if (false) {\n"
        "    let x <int> = 1;\n"
        "  }\n"
        "  return 2;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "2");
}

TEST(ComptimeBranches, UnknownConditionIsAGap) {
    auto p = parse(
        "@special h() <int> {\n"
        "  if (x) {\n"
        "    return 1;\n"
        "  } else {\n"
        "    return 2;\n"
        "  }\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

TEST(ComptimeBranches, NonBoolConditionIsAGap) {
    auto p = parse(
        "@special h() <int> {\n"
        "  if (1) {\n"
        "    return 1;\n"
        "  } else {\n"
        "    return 2;\n"
        "  }\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

TEST(ComptimeBranches, ElseIfChainThreads) {
    auto p = parse(
        "@special h(a: bool, b: bool) <int> {\n"
        "  if (a) {\n"
        "    return 1;\n"
        "  } else if (b) {\n"
        "    return 2;\n"
        "  } else {\n"
        "    return 3;\n"
        "  }\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("a", fin::comptime::Value::makeBool("false"));
    env.bind("b", fin::comptime::Value::makeBool("true"));
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "2");
}

// S2b: the ternary over a comptime-known bool takes its arm. The untaken
// arm never evaluates — the expression analogue of analyze-but-don't-emit.
TEST(ComptimeBranches, TernaryTakesKnownArm) {
    auto p = parse(
        "@special h(c: bool) <int> {\n"
        "  let x <int> = c : 1 ? 2;\n"
        "  return x;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    {
        fin::comptime::Interpreter interp(*p.result.ast);
        fin::comptime::Env env;
        env.bind("c", fin::comptime::Value::makeBool("true"));
        auto r = interp.evaluateBody(*h->body, env);
        ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
        EXPECT_EQ(r.value.text, "1");
    }
    {
        fin::comptime::Interpreter interp(*p.result.ast);
        fin::comptime::Env env;
        env.bind("c", fin::comptime::Value::makeBool("false"));
        auto r = interp.evaluateBody(*h->body, env);
        ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
        EXPECT_EQ(r.value.text, "2");
    }
}

TEST(ComptimeBranches, UntakenTernaryArmNotEvaluated) {
    auto p = parse(
        "@special h() <int> {\n"
        "  let x <int> = true : 1 ? unknown_name;\n"
        "  return x;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "1");
}

TEST(ComptimeBranches, UnknownTernaryConditionIsAGap) {
    auto p = parse(
        "@special h() <int> {\n"
        "  let x <int> = x : 1 ? 2;\n"
        "  return x;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ComptimeBranches, NonBoolTernaryConditionIsAGap) {
    auto p = parse(
        "@special h() <int> {\n"
        "  let x <int> = 1 : 1 ? 2;\n"
        "  return x;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

// S2c (the literal_struct.fin:29 shape): the guard evaluates, but a taken
// `@define` would inject a declaration at comptime — declaration-lifting
// (`compiler.code.lift_to_module_end`), which is Q5, deferred — so it is
// refused naming the question. An untaken `@define` never runs.
TEST(ComptimeBranches, TakenDefineRefusedNamingQ5) {
    auto p = parse(
        "@special h(ready: bool) <int> {\n"
        "  if (ready) {\n"
        "    @define puts(s: string) <noret>;\n"
        "  }\n"
        "  return 1;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("ready", fin::comptime::Value::makeBool("true"));
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_NE(r.detail.find("Q5"), std::string::npos) << r.detail;
}

TEST(ComptimeBranches, UntakenDefineSkipped) {
    auto p = parse(
        "@special h(ready: bool) <int> {\n"
        "  if (ready) {\n"
        "    @define puts(s: string) <noret>;\n"
        "  }\n"
        "  return 1;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("ready", fin::comptime::Value::makeBool("false"));
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "1");
}

// S3 (the amendment's price): a static call-graph cycle check over
// `@special` functions names the cycle; recursion is the only remaining
// route to non-termination, so this — not fuel — is what refuses it.
TEST(ComptimeCycles, MutualPairNamesCycle) {
    auto p = parse(
        "@special a() <int> {\n  return b();\n}\n"
        "@special b() <int> {\n  return a();\n}\n");
    ASSERT_TRUE(p.result.parsed);
    EXPECT_EQ(fin::comptime::Interpreter::findSpecialCycle(*p.result.ast), "a -> b -> a");
}

TEST(ComptimeCycles, SelfRecursionNamesCycle) {
    auto p = parse("@special a() <int> {\n  return a();\n}\n");
    ASSERT_TRUE(p.result.parsed);
    EXPECT_EQ(fin::comptime::Interpreter::findSpecialCycle(*p.result.ast), "a -> a");
}

TEST(ComptimeCycles, AcyclicHelpersReturnEmpty) {
    auto p = parse(
        "@special a() <int> {\n  return b();\n}\n"
        "@special b() <int> {\n  return 1;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    EXPECT_EQ(fin::comptime::Interpreter::findSpecialCycle(*p.result.ast), "");
}

// Plain-`fun` intermediaries are not followed: the static check is over
// `@special` functions, and the dynamic recursion guard below still
// backstops a hang through one. This pins the boundary, not the wish.
TEST(ComptimeCycles, PlainFunIntermediaryNotFollowed) {
    auto p = parse(
        "fun f() <int> {\n  return a();\n}\n"
        "@special a() <int> {\n  return f();\n}\n");
    ASSERT_TRUE(p.result.parsed);
    EXPECT_EQ(fin::comptime::Interpreter::findSpecialCycle(*p.result.ast), "");
}

// The dynamic backstop names the chain too: evaluating into a recursive
// pair gaps with the cycle, never hangs.
TEST(ComptimeCycles, EvaluationIntoPairGapsNamingCycle) {
    auto p = parse(
        "@special a() <int> {\n  return b();\n}\n"
        "@special b() <int> {\n  return a();\n}\n"
        "@special h() <int> {\n  let x <int> = a();\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_NE(r.detail.find("a -> b -> a"), std::string::npos) << r.detail;
}

// Slice 1: the interpretability line holds — loops are a named
// line breach, everything else a named gap; never silent, never guessed.
// (`if`/`else` evaluates since S2; loops stay refused per the amendment.)
TEST(ComptimeValue, ControlFlowStillRefused) {
    auto p = parse("@special h() <int> {\n  while (true) {}\n  return 1;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(fin::comptime::Interpreter::flowBreach(*h->body), "while");
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::LineBreach);
    EXPECT_NE(r.detail.find("while"), std::string::npos) << r.detail;
}

// S1 (operators, ADR 0006 amendment): `==`/`!=` fold on Int/Bool values,
// unary `!` folds on Bool. Anything else stays a named gap, never a guess.
TEST(ComptimeOperators, IntEqualityFolds) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = 1 == 1;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.kind, fin::comptime::ValueKind::Bool);
    EXPECT_EQ(r.value.text, "true");
}

TEST(ComptimeOperators, IntInequalityFoldsBothWays) {
    auto p = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = 1 != 2;\n"
        "  let b <bool> = 2 != 2;\n"
        "  let c <bool> = 1 == 2;\n"
        "  return c;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    // `a` must be true, `b` false; returning `c` (false) proves all three
    // folded rather than one of them gaping.
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "false");
    // Re-run asking for `a` instead: true.
    auto p2 = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = 1 != 2;\n"
        "  return a;\n"
        "}\n");
    ASSERT_TRUE(p2.result.parsed);
    auto* h2 = findSpecial(*p2.result.ast, "h");
    ASSERT_NE(h2, nullptr);
    fin::comptime::Interpreter interp2(*p2.result.ast);
    fin::comptime::Env env2;
    auto r2 = interp2.evaluateBody(*h2->body, env2);
    ASSERT_EQ(r2.status, fin::comptime::BodyStatus::Returned) << r2.detail;
    EXPECT_EQ(r2.value.text, "true");
}

TEST(ComptimeOperators, BoolEqualityFolds) {
    auto p = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = true == true;\n"
        "  let b <bool> = true != false;\n"
        "  return b;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "true");
}

TEST(ComptimeOperators, NotFoldsBool) {
    auto p = parse(
        "@special h(ready: bool) <bool> {\n"
        "  let a <bool> = !true;\n"
        "  let b <bool> = !ready;\n"
        "  return b;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    env.bind("ready", fin::comptime::Value::makeBool("false"));
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "true");
}

// The literal_struct.fin:29 guard shape: `!` over a bool-returning helper
// call folds, so a `@defined`-shaped predicate decides the branch.
TEST(ComptimeOperators, NotFoldsThroughHelperCall) {
    auto p = parse(
        "@special is_ready() <bool> {\n  return false;\n}\n"
        "@special h() <bool> {\n  let guarded <bool> = !is_ready();\n  return guarded;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "true");
}

TEST(ComptimeOperators, MixedKindEqualityIsAGap) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = 1 == true;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

TEST(ComptimeOperators, NotOnIntIsAGap) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = !1;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

// String `==` folds only if a probe needs it; none does, so it stays a gap.
TEST(ComptimeOperators, StringEqualityStaysAGap) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = \"a\" == \"a\";\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

// S1b: `&&`/`||` short-circuit on Bool pairs, int comparison and int
// arithmetic (`+ - * / %`, unary `-`). Division by zero and mixed kinds
// are gaps, never folded guesses.
TEST(ComptimeOperators, LogicalAndOrFold) {
    auto p = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = true && false;\n"
        "  let b <bool> = true || false;\n"
        "  let c <bool> = false || false;\n"
        "  return c;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "false");
}

// Short-circuit matches the backend: the unneeded arm never evaluates, so
// an unknowable name there is not a gap.
TEST(ComptimeOperators, ShortCircuitSkipsUntakenArm) {
    auto p = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = false && unknown_name;\n"
        "  let b <bool> = true || unknown_name;\n"
        "  return b;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "true");
}

TEST(ComptimeOperators, NeededArmStillGaps) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = true && unknown_name;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ComptimeOperators, NonBoolLogicalIsAGap) {
    auto p = parse("@special h() <bool> {\n  let x <bool> = 1 && true;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ComptimeOperators, IntComparisonFolds) {
    auto p = parse(
        "@special h() <bool> {\n"
        "  let a <bool> = 1 < 2;\n"
        "  let b <bool> = 2 <= 2;\n"
        "  let c <bool> = 3 > 4;\n"
        "  let d <bool> = 3 >= 3;\n"
        "  let e <bool> = 2 < 2;\n"
        "  return d;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "true");
}

TEST(ComptimeOperators, IntArithmeticFolds) {
    auto p = parse(
        "@special h() <int> {\n"
        "  let a <int> = 1 + 2;\n"
        "  let b <int> = 10 - 4;\n"
        "  let c <int> = 3 * 4;\n"
        "  let d <int> = 7 / 2;\n"
        "  let e <int> = 7 % 3;\n"
        "  return e;\n"
        "}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.kind, fin::comptime::ValueKind::Int);
    EXPECT_EQ(r.value.text, "1");
}

TEST(ComptimeOperators, ArithmeticMayGoNegative) {
    auto p = parse("@special h() <int> {\n  let x <int> = 5 - 8;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "-3");
}

TEST(ComptimeOperators, UnaryMinusFolds) {
    auto p = parse("@special h() <int> {\n  let x <int> = -5;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned) << r.detail;
    EXPECT_EQ(r.value.text, "-5");
}

TEST(ComptimeOperators, DivisionByZeroIsAGap) {
    auto p = parse("@special h() <int> {\n  let x <int> = 1 / 0;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ComptimeOperators, MixedArithmeticIsAGap) {
    auto p = parse("@special h() <int> {\n  let x <int> = 1 + true;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty());
}

TEST(ComptimeValue, UnsupportedExpressionsGapByName) {
    auto p = parse("@special h() <int> {\n  let x <int> = 1 << 2;\n  return x;\n}\n");
    ASSERT_TRUE(p.result.parsed);
    auto* h = findSpecial(*p.result.ast, "h");
    ASSERT_NE(h, nullptr);
    fin::comptime::Interpreter interp(*p.result.ast);
    fin::comptime::Env env;
    auto r = interp.evaluateBody(*h->body, env);
    EXPECT_EQ(r.status, fin::comptime::BodyStatus::Gap);
    EXPECT_FALSE(r.detail.empty()) << "a gap names what is missing, never silence";
}

// Slice 2: parameterized provider bodies — a `type_metadata`-shaped body
// with parameters threads lets to the projection instead of refusing.
TEST(ComptimeProvider, LetThreadedProjectionIsAccepted) {
    auto r = provCompile(
        "#[use(compiler)]\n#[use(compiler.components.layout)]\n"
        "#[provides(type_metadata)]\n"
        "@special(pub) meta(s: $struct) <quote> {\n"
        "  let t <$struct> = s;\n"
        "  return compiler.layout.pointer_map_quote(t);\n"
        "}\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(ComptimeProvider, CallThreadedProjectionIsAccepted) {
    auto r = provCompile(
        "#[use(compiler)]\n#[use(compiler.components.layout)]\n"
        "fun fwd(s: $struct) <$struct> {\n"
        "  return s;\n"
        "}\n"
        "#[use(compiler)]\n#[use(compiler.components.layout)]\n"
        "#[provides(type_metadata)]\n"
        "@special(pub) meta(s: $struct) <quote> {\n"
        "  let t <$struct> = fwd(s);\n"
        "  return compiler.layout.pointer_map_quote(t);\n"
        "}\n"
        "fun main() <noret> { }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(ComptimeProvider, NonProjectionStillRefused) {
    auto r = provCompile(
        "#[use(compiler)]\n#[use(compiler.components.layout)]\n"
        "#[provides(type_metadata)]\n"
        "@special(pub) meta(s: $struct) <quote> {\n"
        "  return quote { };\n"
        "}\n"
        "fun main() <noret> { }\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(provMessages(stripAnsi(r.err)).find("straight-line answer"), std::string::npos)
        << r.err;
}

// Slice 3: handler bodies adopt the threading — lets and calls before the
// return evaluate instead of refusing, with the splice unchanged.
TEST(ComptimeHandler, LetBeforeReturnQuoteSplices) {
    auto r = provCompile(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(variable_declared)]\n"
        "@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    let m <string> = name;\n"
        "    return quote { printf(\"w5-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(ComptimeHandler, CallBeforeReturnQuoteSplices) {
    auto r = provCompile(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun id(v: int) <int> {\n"
        "  return v;\n"
        "}\n"
        "#[on(variable_declared)]\n"
        "@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    let y <int> = id(7);\n"
        "    return quote { printf(\"w5-called\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(ComptimeHandler, NonQuoteReturnStillRefused) {
    auto r = provCompile(
        "#[on(variable_declared)]\n"
        "@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    return 42;\n"
        "}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    let x <int> = 1;\n"
        "}\n");
    EXPECT_NE(r.exitCode, 0) << r.err;
    EXPECT_NE(provMessages(stripAnsi(r.err)).find("only a quote literal"), std::string::npos)
        << r.err;
}

#ifndef FIN_TESTS_HAVE_BACKEND
#define COMPTIME_BACKEND_TEST(suite, name)                                      \
    TEST(suite, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; }       \
    [[maybe_unused]] static void comptime_backend_body_##suite##_##name()
#else
#define COMPTIME_BACKEND_TEST(suite, name) TEST(suite, name)
#endif

namespace {

struct Built {
    int compileExit = -1;
    std::string compileErr;
    bool ran = false;
    std::string out;
};

Built buildRun(const std::string& code) {
    Built b;
    std::filesystem::path src = uniqueTempPath("fin_comptime_exe", ".fin");
    std::filesystem::path exe = uniqueTempPath("fin_comptime_exe");
    {
        std::ofstream f(src, std::ios::binary);
        f.write(code.data(), (std::streamsize)code.size());
    }
    const FincRun c = runFinc({src.string(), "-o", exe.string()});
    b.compileExit = c.exitCode;
    b.compileErr = stripAnsi(c.err);
    if (b.compileExit == 0 && std::filesystem::exists(exe)) {
        std::filesystem::path outPath = uniqueTempPath("fin_comptime_out");
        std::string cmd = std::string("'") + exe.string() + "' > '" + outPath.string() + "' 2>&1";
        (void)std::system(cmd.c_str());
        b.ran = true;
        std::ifstream f(outPath, std::ios::binary);
        b.out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        std::error_code ec;
        std::filesystem::remove(outPath, ec);
    }
    std::error_code ec;
    std::filesystem::remove(src, ec);
    std::filesystem::remove(exe, ec);
    return b;
}

} // namespace

// S1c: differential probe — every closed-list operator folds (comptime) to
// the value the backend computes (runtime). Bool folds print through the
// runtime ternary `c : 1 ? 0`, so a model disagreement fails here, not in a
// reader's head.
COMPTIME_BACKEND_TEST(ComptimeOperatorsProbe, FoldedValuesMatchRuntimeValues) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"1 == 1", "bool"}, {"1 != 2", "bool"}, {"!false", "bool"},
        {"true && false", "bool"}, {"true || false", "bool"},
        {"1 < 2", "bool"}, {"3 >= 3", "bool"},
        {"1 + 2", "int"}, {"10 - 4", "int"}, {"3 * 4", "int"},
        {"7 / 2", "int"}, {"7 % 3", "int"}, {"5 - 8", "int"}, {"-5", "int"},
    };
    std::string expected;
    for (const auto& cas : cases) {
        const std::string& spelling = cas.first;
        const std::string& type = cas.second;
        auto p = parse("@special h() <" + type + "> {\n  let x <" + type + "> = " +
                       spelling + ";\n  return x;\n}\n");
        ASSERT_TRUE(p.result.parsed) << spelling;
        auto* h = findSpecial(*p.result.ast, "h");
        ASSERT_NE(h, nullptr);
        fin::comptime::Interpreter interp(*p.result.ast);
        fin::comptime::Env env;
        auto r = interp.evaluateBody(*h->body, env);
        ASSERT_EQ(r.status, fin::comptime::BodyStatus::Returned)
            << spelling << ": " << r.detail;
        if (r.value.kind == fin::comptime::ValueKind::Bool)
            expected += (r.value.text == "true" ? "1\n" : "0\n");
        else
            expected += r.value.text + "\n";
    }
    std::string prog = "@define printf(fmt: string, ...) <noret>;\nfun main() <noret> {\n";
    for (const auto& cas : cases) {
        const std::string& spelling = cas.first;
        const std::string& type = cas.second;
        if (type == "bool")
            prog += "    printf(\"%d\\n\", (" + spelling + ") : 1 ? 0);\n";
        else
            prog += "    printf(\"%d\\n\", " + spelling + ");\n";
    }
    prog += "}\n";
    Built b = buildRun(prog);
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_EQ(b.out, expected) << "comptime fold vs runtime value";
}

// A threaded handler's quote runs at the declaration: end-to-end proof the
// splice survived threading rather than merely not refusing.
COMPTIME_BACKEND_TEST(ComptimeHandlerProbe, ThreadedMarkerRunsAtTheDeclaration) {
    Built b = buildRun(
        "@define printf(fmt: string, ...) <noret>;\n"
        "#[on(variable_declared)]\n"
        "@special h_decl(name: string, t: $type, is_mutable: bool) <quote> {\n"
        "    let m <string> = name;\n"
        "    return quote { printf(\"w5-threaded\\n\"); };\n"
        "}\n"
        "compiler.events.enable(h_decl);\n"
        "fun main() <noret> {\n"
        "    printf(\"before\\n\");\n"
        "    let x <int> = 1;\n"
        "    printf(\"after\\n\");\n"
        "}\n");
    EXPECT_EQ(b.compileExit, 0) << b.compileErr;
    ASSERT_TRUE(b.ran) << "compile exit " << b.compileExit << "\n" << b.compileErr;
    EXPECT_NE(b.out.find("w5-threaded"), std::string::npos)
        << "the threaded handler's quote ran at the declaration:\n" << b.out;
}
