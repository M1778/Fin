#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fin {
class BinaryOp;
class Block;
class CastExpression;
class Expression;
class ArrayAccess;
class ArrayLiteral;
class FunctionCall;
class IfStatement;
class MemberAccess;
class MethodCall;
class Program;
class QuoteExpression;
class StructDeclaration;
class StructInstantiation;
class UnaryOp;
class VariableDeclaration;

namespace comptime {

// The comptime value model, first step (ADR 0006): straight-line threading
// of literals + lets + calls, plus the closed operator list over Int/Bool
// values (`==`/`!=`, `!`, `&&`/`||`, int comparison, int arithmetic) and
// `==`/`!=` over String values (decoded content, the runtime rule), and
// branches over comptime-known bools (`if`/`else`, ternary). Parameters bind
// (the unlock), helpers run, and the interpretability line holds — no loops,
// no recursion. Anything outside the subset is a named gap or a named line
// breach: never silent, never guessed. The full interpreter (arbitrary calls,
// component execution) is a later wave and must extend this file, not bypass
// it.
//
// G4 (call-site coverage): struct and array values thread too. A
// `Name{ field: value, ... }` builds a Struct (explicit fields evaluate;
// unlisted members fall back to their declared defaults, evaluated closed),
// `[a, b]` builds an Array, `.field` / `[i]` read back out of them, and a
// dot-call on a Struct runs the declared method with `self` bound. Casts fold
// only when they claim nothing new (same-kind scalars, or any cast of an
// opaque), and `compiler.components.<c>.present()/version()/name()` answer
// from the static component table. Instantiating a `$struct` *value* (`st{}`)
// stays refused: its type is known only at compile time (wave 4).
enum class ValueKind { Int, String, Bool, Quote, Opaque, Struct, Array };

struct Value {
    ValueKind kind = ValueKind::Opaque;
    // A literal's spelling ("42", "true"), an opaque value's
    // identity (a bound parameter's name, a payload tag), or a string's
    // decoded content (quotes stripped, escapes decoded at literal eval, so
    // a bound handler name and a literal spelling compare by content).
    // Opaques compare
    // by this text, which is what lets `let t = s;` thread a subject.
    std::string text;
    // Non-owning: the tree outlives every evaluation, so a quote value
    // names the literal it came from and the caller clones on splice.
    const QuoteExpression* quote = nullptr;
    // Struct only: the instantiated type's name and its fields in source
    // order (explicit fields first, then declared defaults for the rest).
    // Values are type-erased — generics need no substitution to evaluate.
    std::string struct_name;
    std::vector<std::pair<std::string, Value>> fields;
    // Array only, in element order.
    std::vector<Value> elements;

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
    static Value makeStruct(std::string name,
                            std::vector<std::pair<std::string, Value>> fields) {
        Value v;
        v.kind = ValueKind::Struct;
        v.struct_name = std::move(name);
        v.fields = std::move(fields);
        return v;
    }
    static Value makeArray(std::vector<Value> elements) {
        Value v;
        v.kind = ValueKind::Array;
        v.elements = std::move(elements);
        return v;
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
    // Rebinds a plain `name = value` (I-G1): the right side evaluates in
    // `env` and the name rebinds on Ok, so `x = ...; return x` sees the new
    // value. Any other assignment shape (compound `+=`, an index or member
    // store) is a named gap, never silent: there is no array or member
    // model to write through.
    ExprStatus evaluateAssign(const BinaryOp& node, Env& env, std::string* detail);
    BodyResult evaluateBody(const Block& body, Env& env);
    // One `if`/`else-if` step (S2): Empty fell through, anything else is
    // the arm's answer. The untaken arm never runs. Public so handler
    // firing (I-G3) takes the same arm the interpreter would, with the
    // same depth and acyclicity guards, rather than restating the rule.
    BodyResult evaluateIf(const IfStatement& node, Env& env);

private:
    static constexpr int kMaxDepth = 64;

    ExprResult evaluateCall(const std::string& name,
                            const std::vector<std::unique_ptr<Expression>>& args,
                            const Env& callerEnv);
    // The closed operator list (S1): `!` on Bool, `==`/`!=` on Int/Bool
    // pairs and on String pairs (decoded content), `&&`/`||` short-circuiting
    // on Bool pairs, int comparison and int arithmetic. Anything else is a
    // named gap.
    ExprResult evaluateUnary(const UnaryOp& node, const Env& env);
    ExprResult evaluateBinary(const BinaryOp& node, const Env& env);
    // G4: the call-site forms. Construction evaluates its fields (plus
    // declared defaults, closed) into a Struct/Array value; member and index
    // reads project back out; a cast folds only when it claims nothing new;
    // a dot-call on a Struct runs the declared method with `self` bound under
    // the same depth and acyclicity guards as a free call; a
    // `compiler.components.<c>.present()/version()/name()` answers from the
    // static table. Anything else in these shapes is a named gap.
    ExprResult evaluateStruct(const StructInstantiation& node, const Env& env);
    ExprResult evaluateMember(const MemberAccess& node, const Env& env);
    ExprResult evaluateArray(const ArrayLiteral& node, const Env& env);
    ExprResult evaluateIndex(const ArrayAccess& node, const Env& env);
    ExprResult evaluateCast(const CastExpression& node, const Env& env);
    ExprResult evaluateMethod(const MethodCall& node, const Env& env);
    // The straight-line body of the named helper, with its parameter names.
    // Null when no plain or `@special` function of that name is declared.
    const Block* findBody(const std::string& name,
                          std::vector<std::string>* paramsOut) const;
    // The struct declaration of that name, in declaration order. Null when
    // no `struct` of that name is declared — which is also how a `$struct`
    // *value* in `st{}` position is told apart from a type.
    const StructDeclaration* findStruct(const std::string& name) const;

    const Program& program_;
    std::unordered_set<std::string> activeCalls_;
    // The call chain behind activeCalls_, in entry order: a re-entered name
    // reports the cycle it closes ("a -> b -> a"), not just its own name.
    std::vector<std::string> callStack_;
    int depth_ = 0;
};

} // namespace comptime
} // namespace fin
