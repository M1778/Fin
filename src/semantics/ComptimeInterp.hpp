#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fin {
class BinaryOp;
class Block;
class Expression;
class IfStatement;
class Program;
class QuoteExpression;
class UnaryOp;
class VariableDeclaration;

namespace comptime {

// The comptime value model, first step (ADR 0006): straight-line threading
// of literals + lets + calls, plus the closed operator list over Int/Bool
// values (`==`/`!=`, `!`, `&&`/`||`, int comparison, int arithmetic) and
// branches over comptime-known bools (`if`/`else`, ternary). Parameters bind
// (the unlock), helpers run, and the interpretability line holds — no loops,
// no recursion. Anything outside the subset is a named gap or a named line
// breach: never silent, never guessed. The full interpreter (arbitrary calls,
// component execution) is a later wave and must extend this file, not bypass
// it.
enum class ValueKind { Int, String, Bool, Quote, Opaque };

struct Value {
    ValueKind kind = ValueKind::Opaque;
    // A literal's spelling ("42", "hi", "true"), or an opaque value's
    // identity (a bound parameter's name, a payload tag). Opaques compare
    // by this text, which is what lets `let t = s;` thread a subject.
    std::string text;
    // Non-owning: the tree outlives every evaluation, so a quote value
    // names the literal it came from and the caller clones on splice.
    const QuoteExpression* quote = nullptr;

    static Value makeInt(std::string spelling) {
        return Value{ValueKind::Int, std::move(spelling), nullptr};
    }
    static Value makeString(std::string spelling) {
        return Value{ValueKind::String, std::move(spelling), nullptr};
    }
    static Value makeBool(std::string spelling) {
        return Value{ValueKind::Bool, std::move(spelling), nullptr};
    }
    static Value makeQuote(const QuoteExpression* quoted) {
        return Value{ValueKind::Quote, {}, quoted};
    }
    static Value makeOpaque(std::string identity) {
        return Value{ValueKind::Opaque, std::move(identity), nullptr};
    }
};

class Env {
public:
    void bind(const std::string& name, Value value) { map_[name] = std::move(value); }
    const Value* lookup(const std::string& name) const {
        const auto it = map_.find(name);
        return it != map_.end() ? &it->second : nullptr;
    }

private:
    std::unordered_map<std::string, Value> map_;
};

enum class BodyStatus { Returned, Empty, Blame, Gap, LineBreach };

struct BodyResult {
    BodyStatus status = BodyStatus::Gap;
    Value value;
    // What is missing (gap), which control form (line breach), or the
    // blame's message. Always non-empty unless the body was empty.
    std::string detail;
};

enum class ExprStatus { Ok, Gap, LineBreach };

struct ExprResult {
    ExprStatus status = ExprStatus::Gap;
    Value value;
    std::string detail;
};

class Interpreter {
public:
    explicit Interpreter(const Program& program) : program_(program) {}

    // The first loop form in the body ("while", "for", "foreach"), or ""
    // when the body has none. `if`/`else` evaluates (S2) and is never a
    // breach; quote and lambda bodies are data and runtime code
    // respectively, never comptime control flow, so neither subtree is
    // entered.
    static std::string flowBreach(const Block& body);

    // The static call-graph cycle check over `@special` functions (the
    // amendment's price: recursion is the only remaining route to
    // non-termination, so this — not fuel — refuses it). "" when acyclic,
    // else the cycle named "a -> b -> a" in declaration order. Edges are
    // direct calls by name; plain-`fun` intermediaries are not followed
    // (the dynamic recursion guard in calls still backstops a hang through
    // one).
    static std::string findSpecialCycle(const Program& program);

    ExprResult evaluateExpression(const Expression& expr, const Env& env);
    // Binds the declaration on Ok. The detail names the form on Gap.
    ExprStatus evaluateDeclaration(const VariableDeclaration& decl, Env& env,
                                   std::string* detail);
    BodyResult evaluateBody(const Block& body, Env& env);

private:
    static constexpr int kMaxDepth = 64;

    ExprResult evaluateCall(const std::string& name,
                            const std::vector<std::unique_ptr<Expression>>& args,
                            const Env& callerEnv);
    // The closed operator list (S1): `!` on Bool, `==`/`!=` on Int/Bool
    // pairs, `&&`/`||` short-circuiting on Bool pairs, int comparison and
    // int arithmetic. Anything else is a named gap.
    ExprResult evaluateUnary(const UnaryOp& node, const Env& env);
    ExprResult evaluateBinary(const BinaryOp& node, const Env& env);
    // One `if`/`else-if` step (S2): Empty fell through, anything else is
    // the arm's answer. The untaken arm never runs.
    BodyResult evaluateIf(const IfStatement& node, Env& env);
    // The straight-line body of the named helper, with its parameter names.
    // Null when no plain or `@special` function of that name is declared.
    const Block* findBody(const std::string& name,
                          std::vector<std::string>* paramsOut) const;

    const Program& program_;
    std::unordered_set<std::string> activeCalls_;
    // The call chain behind activeCalls_, in entry order: a re-entered name
    // reports the cycle it closes ("a -> b -> a"), not just its own name.
    std::vector<std::string> callStack_;
    int depth_ = 0;
};

} // namespace comptime
} // namespace fin
