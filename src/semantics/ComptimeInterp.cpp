#include "ComptimeInterp.hpp"

#include <cstdint>
#include <functional>
#include <limits>

#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/DefineDecl.hpp"
#include "../ast/decls/FunctionDecl.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/BinaryOp.hpp"
#include "../ast/exprs/FunctionCall.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/Lambda.hpp"
#include "../ast/exprs/Literal.hpp"
#include "../ast/exprs/MiscExpr.hpp"
#include "../ast/exprs/UnaryOp.hpp"
#include "../ast/stmts/ControlFlow.hpp"
#include "../ast/stmts/ErrorHandling.hpp"
#include "../ast/stmts/Statement.hpp"
#include "../ast/stmts/VariableDecl.hpp"

// The comptime value model, second step (see the header for the contract).
//
// Straight-line threading plus the closed operator list: `==`/`!=` on
// Int/Bool pairs, `!` on Bool, `&&`/`||` short-circuiting on Bool pairs, int
// comparison and int arithmetic. Literals evaluate, lets bind, identifiers
// read the environment, quotes become values, branches over known bools take
// their arm, and calls into plain or `@special` helpers with evaluable bodies
// run with their arguments bound. Loops stay refused, and everything else is
// a named gap. Nothing here diagnoses: results carry the name of what is
// missing and the caller — which owns its diagnostic wording — reports it,
// so adopting this file cannot reword a diagnostic the corpus already pins.
namespace fin::comptime {

namespace {

// A Bool value's meaning, by its only two spellings.
bool asBool(const Value& value, bool* out) {
    if (value.kind != ValueKind::Bool) return false;
    if (value.text == "true") {
        *out = true;
        return true;
    }
    if (value.text == "false") {
        *out = false;
        return true;
    }
    return false;
}

// An Int value's meaning. The lexer admits decimal digits and nothing else,
// so a full-match decimal parse is exact; anything else is a gap, never a
// guess. The model is the host int64: signed, two's complement, wrapping on
// overflow exactly as the backend's integer operations do.
bool asInt(const Value& value, std::int64_t* out) {
    if (value.kind != ValueKind::Int) return false;
    const std::string& text = value.text;
    if (text.empty()) return false;
    std::size_t pos = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        pos = 1;
        if (text.size() == 1) return false;
    }
    std::uint64_t magnitude = 0;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (c < '0' || c > '9') return false;
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        magnitude = magnitude * 10 + digit;
    }
    if (!negative) {
        if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return false;
        *out = static_cast<std::int64_t>(magnitude);
        return true;
    }
    if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1) return false;
    if (magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1)
        *out = std::numeric_limits<std::int64_t>::min();
    else
        *out = -static_cast<std::int64_t>(magnitude);
    return true;
}

std::string spellBool(bool b) { return b ? "true" : "false"; }

std::string spellInt(std::int64_t n) { return std::to_string(n); }

// The loop forms, by source spelling. `if`/`else` evaluates (S2) and is
// never a breach; quote and lambda bodies are skipped: a quote is data to
// splice, a lambda body is runtime code, and neither is a comptime branch.
class FlowFinder : public StructuralWalk {
public:
    std::string found;
    bool enter(ASTNode& node) override {
        if (!found.empty()) return false;
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        switch (node.kind()) {
            case NodeKind::WhileLoop: found = "while"; return false;
            case NodeKind::ForLoop: found = "for"; return false;
            case NodeKind::ForeachLoop: found = "foreach"; return false;
            default: return true;
        }
    }
};

// The callee names in a body, in source order. Quotes are data to splice
// and lambdas are runtime code, so neither subtree is entered — the same
// rule FlowFinder holds. `@name(...)` and plain `name(...)` both count: a
// `@special` resolves under either spelling.
class CallNameCollector : public StructuralWalk {
public:
    std::vector<std::string> names;
    bool enter(ASTNode& node) override {
        if (dynamic_cast<QuoteExpression*>(&node)) return false;
        if (dynamic_cast<LambdaExpression*>(&node)) return false;
        if (const auto* call = dynamic_cast<const FunctionCall*>(&node)) names.push_back(call->name);
        else if (const auto* inv = dynamic_cast<const MacroInvocation*>(&node))
            names.push_back(inv->name);
        return true;
    }
};

} // namespace

std::string Interpreter::flowBreach(const Block& body) {
    FlowFinder finder;
    finder.walk(const_cast<Block&>(body));
    return finder.found;
}

std::string Interpreter::findSpecialCycle(const Program& program) {
    // Declaration order, so the reported cycle is deterministic. The first
    // body wins a duplicated name; duplicates are the analyzer's error, and
    // the graph must not depend on which one it kept.
    std::vector<std::string> order;
    std::unordered_map<std::string, const Block*> bodies;
    for (const auto& stmt : program.statements) {
        const auto* special = dynamic_cast<const SpecialDeclaration*>(stmt.get());
        if (!special || !special->body) continue;
        if (bodies.emplace(special->name, special->body.get()).second)
            order.push_back(special->name);
    }
    std::unordered_map<std::string, std::vector<std::string>> edges;
    for (const auto& name : order) {
        CallNameCollector calls;
        calls.walk(const_cast<Block&>(*bodies[name]));
        for (const auto& callee : calls.names)
            if (bodies.count(callee) != 0) edges[name].push_back(callee);
    }
    std::unordered_map<std::string, int> state;  // 0 unvisited, 1 on stack, 2 done
    std::vector<std::string> stack;
    std::string cycle;
    std::function<bool(const std::string&)> visit = [&](const std::string& node) -> bool {
        state[node] = 1;
        stack.push_back(node);
        for (const auto& next : edges[node]) {
            if (state[next] == 1) {
                std::string out;
                bool recording = false;
                for (const auto& frame : stack) {
                    if (frame == next) recording = true;
                    if (recording) {
                        if (!out.empty()) out += " -> ";
                        out += frame;
                    }
                }
                cycle = out + " -> " + next;
                return true;
            }
            if (state[next] == 0 && visit(next)) return true;
        }
        stack.pop_back();
        state[node] = 2;
        return false;
    };
    for (const auto& name : order) {
        if (state[name] == 0 && visit(name)) return cycle;
    }
    return "";
}

const Block* Interpreter::findBody(const std::string& name,
                                   std::vector<std::string>* paramsOut) const {
    for (const auto& stmt : program_.statements) {
        if (const auto* fun = dynamic_cast<const FunctionDeclaration*>(stmt.get())) {
            if (fun->name != name) continue;
            if (paramsOut) {
                paramsOut->clear();
                for (const auto& param : fun->params) paramsOut->push_back(param->name);
            }
            return fun->body.get();
        }
        if (const auto* special = dynamic_cast<const SpecialDeclaration*>(stmt.get())) {
            if (special->name != name) continue;
            if (paramsOut) {
                paramsOut->clear();
                for (const auto& param : special->params) paramsOut->push_back(param->name);
            }
            return special->body.get();
        }
    }
    return nullptr;
}

ExprResult Interpreter::evaluateCall(const std::string& name,
                                     const std::vector<std::unique_ptr<Expression>>& args,
                                     const Env& callerEnv) {
    std::vector<Value> argValues;
    argValues.reserve(args.size());
    for (const auto& arg : args) {
        if (!arg) return ExprResult{ExprStatus::Gap, {}, "missing argument in call '" + name + "'"};
        ExprResult evaluated = evaluateExpression(*arg, callerEnv);
        if (evaluated.status != ExprStatus::Ok) return evaluated;
        argValues.push_back(std::move(evaluated.value));
    }
    std::vector<std::string> params;
    const Block* body = findBody(name, &params);
    if (!body) return ExprResult{ExprStatus::Gap, {}, "call to unknown '" + name + "'"};
    // The no-hang guarantee, both halves: evaluable bodies are bounded by
    // the program text, and calls do not recurse. A recursive call is a
    // named gap — the cycle it closes, never a hang. The static
    // findSpecialCycle above names the same cycle without running anything;
    // this guard backstops whatever it cannot see (calls through plain
    // `fun` intermediaries, which the static graph does not follow).
    if (activeCalls_.count(name) != 0) {
        std::string chain;
        bool recording = false;
        for (const auto& frame : callStack_) {
            if (frame == name) recording = true;
            if (recording) {
                if (!chain.empty()) chain += " -> ";
                chain += frame;
            }
        }
        chain += " -> " + name;
        return ExprResult{ExprStatus::Gap, {},
                          "recursive call '" + name + "' in cycle '" + chain + "'"};
    }
    if (depth_ >= kMaxDepth)
        return ExprResult{ExprStatus::Gap, {}, "recursive call '" + name + "'"};
    if (params.size() != argValues.size())
        return ExprResult{ExprStatus::Gap, {},
                          "wrong arity for '" + name + "'"};
    const std::string flow = flowBreach(*body);
    if (!flow.empty()) return ExprResult{ExprStatus::LineBreach, {}, flow};
    Env calleeEnv;
    for (std::size_t i = 0; i < params.size(); ++i)
        calleeEnv.bind(params[i], std::move(argValues[i]));
    activeCalls_.insert(name);
    callStack_.push_back(name);
    ++depth_;
    BodyResult ran = evaluateBody(*body, calleeEnv);
    --depth_;
    callStack_.pop_back();
    activeCalls_.erase(name);
    switch (ran.status) {
        case BodyStatus::Returned: return ExprResult{ExprStatus::Ok, std::move(ran.value), {}};
        case BodyStatus::LineBreach: return ExprResult{ExprStatus::LineBreach, {}, ran.detail};
        case BodyStatus::Blame:
            return ExprResult{ExprStatus::Gap, {}, "blame in '" + name + "'"};
        case BodyStatus::Empty:
            return ExprResult{ExprStatus::Gap, {}, "no return in '" + name + "'"};
        case BodyStatus::Gap: return ExprResult{ExprStatus::Gap, {}, ran.detail};
    }
    return ExprResult{ExprStatus::Gap, {}, "call '" + name + "' did not evaluate"};
}

ExprResult Interpreter::evaluateExpression(const Expression& expr, const Env& env) {
    if (const auto* lit = dynamic_cast<const Literal*>(&expr)) {
        switch (lit->kind) {
            case ASTTokenKind::INTEGER: return ExprResult{ExprStatus::Ok, Value::makeInt(lit->value), {}};
            case ASTTokenKind::STRING_LITERAL:
                return ExprResult{ExprStatus::Ok, Value::makeString(lit->value), {}};
            case ASTTokenKind::BOOL: return ExprResult{ExprStatus::Ok, Value::makeBool(lit->value), {}};
            case ASTTokenKind::FLOAT: return ExprResult{ExprStatus::Gap, {}, "float literal"};
            case ASTTokenKind::CHAR_LITERAL: return ExprResult{ExprStatus::Gap, {}, "char literal"};
            case ASTTokenKind::KW_NULL: return ExprResult{ExprStatus::Gap, {}, "null"};
            case ASTTokenKind::M1778: return ExprResult{ExprStatus::Gap, {}, "m1778"};
            default: return ExprResult{ExprStatus::Gap, {}, "literal"};
        }
    }
    if (const auto* id = dynamic_cast<const Identifier*>(&expr)) {
        if (const Value* bound = env.lookup(id->name)) return ExprResult{ExprStatus::Ok, *bound, {}};
        return ExprResult{ExprStatus::Gap, {}, "unknown name '" + id->name + "'"};
    }
    if (const auto* quote = dynamic_cast<const QuoteExpression*>(&expr))
        return ExprResult{ExprStatus::Ok, Value::makeQuote(quote), {}};
    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr))
        return evaluateCall(call->name, call->args, env);
    if (const auto* inv = dynamic_cast<const MacroInvocation*>(&expr))
        return evaluateCall(inv->name, inv->args, env);
    if (dynamic_cast<const UnaryOp*>(&expr))
        return evaluateUnary(static_cast<const UnaryOp&>(expr), env);
    if (dynamic_cast<const BinaryOp*>(&expr))
        return evaluateBinary(static_cast<const BinaryOp&>(expr), env);
    if (const auto* tern = dynamic_cast<const TernaryOp*>(&expr)) {
        // S2: over a comptime-known bool only. The untaken arm never
        // evaluates, so an unknowable name there is not a gap.
        if (!tern->condition || !tern->true_expr || !tern->false_expr)
            return ExprResult{ExprStatus::Gap, {}, "a ternary needs a condition and two arms"};
        ExprResult cond = evaluateExpression(*tern->condition, env);
        if (cond.status != ExprStatus::Ok) return cond;
        bool take = false;
        if (!asBool(cond.value, &take))
            return ExprResult{ExprStatus::Gap, {}, "a ternary needs a bool condition"};
        return evaluateExpression(take ? *tern->true_expr : *tern->false_expr, env);
    }
    // Method, static-method, macro and every other expression shape need
    // the full interpreter. Named by node kind, never silently dropped.
    return ExprResult{ExprStatus::Gap, {}, std::string(nodeKindName(expr.kind()))};
}

ExprResult Interpreter::evaluateUnary(const UnaryOp& node, const Env& env) {
    // S1, closed: `!` on Bool and unary `-` on Int. Anything else
    // (`~`/`&`/`++`/`--`/`*`/`?`, postfix forms) is a named gap.
    if (node.op != ASTTokenKind::NOT && node.op != ASTTokenKind::MINUS)
        return ExprResult{ExprStatus::Gap, {}, std::string(nodeKindName(node.kind()))};
    if (!node.operand) return ExprResult{ExprStatus::Gap, {}, "missing operand in unary expression"};
    ExprResult inner = evaluateExpression(*node.operand, env);
    if (inner.status != ExprStatus::Ok) return inner;
    if (node.op == ASTTokenKind::NOT) {
        bool b = false;
        if (!asBool(inner.value, &b))
            return ExprResult{ExprStatus::Gap, {}, "'!' needs a bool"};
        return ExprResult{ExprStatus::Ok, Value::makeBool(spellBool(!b)), {}};
    }
    std::int64_t n = 0;
    if (!asInt(inner.value, &n))
        return ExprResult{ExprStatus::Gap, {}, "unary '-' needs an int"};
    // Wrapping negation, exactly the backend's `sub 0, n`: INT64_MIN stays
    // INT64_MIN rather than trapping, so there is nothing to refuse.
    const std::uint64_t negated = 0 - static_cast<std::uint64_t>(n);
    return ExprResult{ExprStatus::Ok, Value::makeInt(spellInt(static_cast<std::int64_t>(negated))), {}};
}

ExprResult Interpreter::evaluateBinary(const BinaryOp& node, const Env& env) {
    // S1, closed: `==`/`!=` on Int/Bool pairs, `&&`/`||` short-circuiting
    // on Bool pairs, int comparison and int arithmetic. Anything else is a
    // named gap — shifts, bit ops and string orderings never fold.
    if (!node.left || !node.right)
        return ExprResult{ExprStatus::Gap, {}, "missing operand in binary expression"};
    if (node.op == ASTTokenKind::AND || node.op == ASTTokenKind::OR) {
        // Short-circuit like the backend: the unneeded arm never evaluates,
        // so an unknowable name there is not a gap.
        ExprResult left = evaluateExpression(*node.left, env);
        if (left.status != ExprStatus::Ok) return left;
        bool leftBool = false;
        if (!asBool(left.value, &leftBool))
            return ExprResult{ExprStatus::Gap, {}, "'&&' and '||' need bools"};
        if (node.op == ASTTokenKind::AND && !leftBool)
            return ExprResult{ExprStatus::Ok, Value::makeBool("false"), {}};
        if (node.op == ASTTokenKind::OR && leftBool)
            return ExprResult{ExprStatus::Ok, Value::makeBool("true"), {}};
        ExprResult right = evaluateExpression(*node.right, env);
        if (right.status != ExprStatus::Ok) return right;
        bool rightBool = false;
        if (!asBool(right.value, &rightBool))
            return ExprResult{ExprStatus::Gap, {}, "'&&' and '||' need bools"};
        return ExprResult{ExprStatus::Ok, Value::makeBool(spellBool(rightBool)), {}};
    }
    if (node.op == ASTTokenKind::EQEQ || node.op == ASTTokenKind::NOTEQ) {
        ExprResult left = evaluateExpression(*node.left, env);
        if (left.status != ExprStatus::Ok) return left;
        ExprResult right = evaluateExpression(*node.right, env);
        if (right.status != ExprStatus::Ok) return right;
        bool equal = false;
        if (left.value.kind == ValueKind::Int && right.value.kind == ValueKind::Int) {
            std::int64_t a = 0;
            std::int64_t b = 0;
            if (!asInt(left.value, &a) || !asInt(right.value, &b))
                return ExprResult{ExprStatus::Gap, {}, "'==' on integers outside comptime range"};
            equal = (a == b);
        } else if (left.value.kind == ValueKind::Bool && right.value.kind == ValueKind::Bool) {
            equal = (left.value.text == right.value.text);
        } else {
            return ExprResult{ExprStatus::Gap, {}, "'==' needs two ints or two bools"};
        }
        const bool result = (node.op == ASTTokenKind::EQEQ) ? equal : !equal;
        return ExprResult{ExprStatus::Ok, Value::makeBool(spellBool(result)), {}};
    }
    if (node.op == ASTTokenKind::LT || node.op == ASTTokenKind::GT ||
        node.op == ASTTokenKind::LTEQ || node.op == ASTTokenKind::GTEQ) {
        // Signed comparison, the backend's SCmp on signed integers. The
        // analyzer refuses negative constants against unsigned types, so an
        // accepted program never asks this model to compare a negative
        // spelling with unsigned lowering — and `==` above is width-agnostic
        // either way.
        ExprResult left = evaluateExpression(*node.left, env);
        if (left.status != ExprStatus::Ok) return left;
        ExprResult right = evaluateExpression(*node.right, env);
        if (right.status != ExprStatus::Ok) return right;
        std::int64_t a = 0;
        std::int64_t b = 0;
        if (!asInt(left.value, &a) || !asInt(right.value, &b))
            return ExprResult{ExprStatus::Gap, {}, "comparison needs two ints"};
        bool result = false;
        switch (node.op) {
            case ASTTokenKind::LT: result = (a < b); break;
            case ASTTokenKind::GT: result = (a > b); break;
            case ASTTokenKind::LTEQ: result = (a <= b); break;
            default: result = (a >= b); break;
        }
        return ExprResult{ExprStatus::Ok, Value::makeBool(spellBool(result)), {}};
    }
    if (node.op == ASTTokenKind::PLUS || node.op == ASTTokenKind::MINUS ||
        node.op == ASTTokenKind::MULT || node.op == ASTTokenKind::DIV ||
        node.op == ASTTokenKind::MOD) {
        // Wrapping int64 arithmetic, the backend's `add`/`sub`/`mul` and
        // signed `sdiv`/`srem`. Division by zero is a gap — the backend
        // traps, so there is no value to fold — and INT64_MIN divided or
        // remaindered by -1 is a gap too, because that quotient is backend
        // poison rather than a number.
        ExprResult left = evaluateExpression(*node.left, env);
        if (left.status != ExprStatus::Ok) return left;
        ExprResult right = evaluateExpression(*node.right, env);
        if (right.status != ExprStatus::Ok) return right;
        std::int64_t a = 0;
        std::int64_t b = 0;
        if (!asInt(left.value, &a) || !asInt(right.value, &b))
            return ExprResult{ExprStatus::Gap, {}, "arithmetic needs two ints"};
        const std::uint64_t ua = static_cast<std::uint64_t>(a);
        const std::uint64_t ub = static_cast<std::uint64_t>(b);
        if ((node.op == ASTTokenKind::DIV || node.op == ASTTokenKind::MOD) && b == 0)
            return ExprResult{ExprStatus::Gap, {}, "division by zero"};
        if ((node.op == ASTTokenKind::DIV || node.op == ASTTokenKind::MOD) && b == -1 &&
            a == std::numeric_limits<std::int64_t>::min())
            return ExprResult{ExprStatus::Gap, {}, "overflow in division"};
        std::uint64_t raw = 0;
        switch (node.op) {
            case ASTTokenKind::PLUS: raw = ua + ub; break;
            case ASTTokenKind::MINUS: raw = ua - ub; break;
            case ASTTokenKind::MULT: raw = ua * ub; break;
            case ASTTokenKind::DIV: raw = static_cast<std::uint64_t>(a / b); break;
            default: raw = static_cast<std::uint64_t>(a % b); break;
        }
        return ExprResult{ExprStatus::Ok, Value::makeInt(spellInt(static_cast<std::int64_t>(raw))), {}};
    }
    return ExprResult{ExprStatus::Gap, {}, std::string(nodeKindName(node.kind()))};
}

ExprStatus Interpreter::evaluateDeclaration(const VariableDeclaration& decl, Env& env,
                                            std::string* detail) {
    if (!decl.initializer) {
        if (detail) *detail = "declares '" + decl.name + "' with no initialiser";
        return ExprStatus::Gap;
    }
    ExprResult init = evaluateExpression(*decl.initializer, env);
    if (init.status != ExprStatus::Ok) {
        if (detail) *detail = std::move(init.detail);
        return init.status;
    }
    env.bind(decl.name, std::move(init.value));
    return ExprStatus::Ok;
}

BodyResult Interpreter::evaluateBody(const Block& body, Env& env) {
    if (body.statements.empty()) return BodyResult{BodyStatus::Empty, {}, {}};
    const std::string flow = flowBreach(body);
    if (!flow.empty()) return BodyResult{BodyStatus::LineBreach, {}, flow};
    for (const auto& stmt : body.statements) {
        if (const auto* decl = dynamic_cast<const VariableDeclaration*>(stmt.get())) {
            std::string detail;
            const ExprStatus status = evaluateDeclaration(*decl, env, &detail);
            if (status != ExprStatus::Ok) {
                return BodyResult{status == ExprStatus::LineBreach ? BodyStatus::LineBreach
                                                                   : BodyStatus::Gap,
                                  {}, std::move(detail)};
            }
            continue;
        }
        if (const auto* ret = dynamic_cast<const ReturnStatement*>(stmt.get())) {
            if (!ret->value) return BodyResult{BodyStatus::Empty, {}, {}};
            ExprResult value = evaluateExpression(*ret->value, env);
            if (value.status != ExprStatus::Ok) {
                return BodyResult{value.status == ExprStatus::LineBreach
                                      ? BodyStatus::LineBreach
                                      : BodyStatus::Gap,
                                  {}, std::move(value.detail)};
            }
            return BodyResult{BodyStatus::Returned, std::move(value.value), {}};
        }
        if (const auto* blame = dynamic_cast<const BlameStatement*>(stmt.get())) {
            std::string detail = "blame";
            if (blame->message) {
                if (const auto* lit = dynamic_cast<const Literal*>(blame->message.get()))
                    detail = "blame: \"" + lit->value + "\"";
            }
            return BodyResult{BodyStatus::Blame, {}, std::move(detail)};
        }
        if (const auto* exprStmt = dynamic_cast<const ExpressionStatement*>(stmt.get())) {
            if (!exprStmt->expr)
                return BodyResult{BodyStatus::Gap, {}, "empty statement"};
            ExprResult discarded = evaluateExpression(*exprStmt->expr, env);
            if (discarded.status != ExprStatus::Ok) {
                return BodyResult{discarded.status == ExprStatus::LineBreach
                                      ? BodyStatus::LineBreach
                                      : BodyStatus::Gap,
                                  {}, std::move(discarded.detail)};
            }
            continue;
        }
        if (const auto* ifStmt = dynamic_cast<const IfStatement*>(stmt.get())) {
            BodyResult branch = evaluateIf(*ifStmt, env);
            // Empty fell off the taken arm (or had no `else` to take): the
            // statements after the `if` still run.
            if (branch.status == BodyStatus::Empty) continue;
            return branch;
        }
        if (const auto* def = dynamic_cast<const DefineDeclaration*>(stmt.get())) {
            // S2: a taken `@define` would inject a declaration at comptime,
            // which needs declaration-lifting
            // (`compiler.code.lift_to_module_end`) — Q5, deferred — so it is
            // refused naming the question, never run and never silently
            // dropped. The analyzer still publishes guarded `@define`s
            // (Soundness_SpecialCalls.GuardedDefinePublishesToSubsequentCode);
            // this is only what *executing* one would mean.
            return BodyResult{BodyStatus::Gap, {},
                              "@define '" + def->name +
                                  "' injects a declaration: no declaration-lifting at comptime "
                                  "(compiler.code.lift_to_module_end, Q5 deferred)"};
        }
        return BodyResult{BodyStatus::Gap, {}, std::string(nodeKindName(stmt->kind()))};
    }
    return BodyResult{BodyStatus::Empty, {}, {}};
}

BodyResult Interpreter::evaluateIf(const IfStatement& node, Env& env) {
    // S2: the condition must be a comptime-known bool; the taken arm runs in
    // the caller's environment so its lets bind past it, and the untaken arm
    // never runs — analyze-but-don't-emit (both arms typecheck in the
    // analyzer walk, only the taken one evaluates here).
    //
    // Host-taint hole, recorded not built: a condition threaded through a
    // helper call carries no taint in this model, so a host read behind a
    // call folds like any other bool. warnOnHostBranch
    // (Analyzer_CompilerApi.cpp) stays syntactic for the same reason and
    // records the same hole; taint in the value model is a later wave, and
    // silently treating a helper-called bool as host-clean would be the bug
    // this note refuses to write.
    if (!node.condition || !node.then_block)
        return BodyResult{BodyStatus::Gap, {}, "an 'if' needs a condition and a body"};
    ExprResult cond = evaluateExpression(*node.condition, env);
    if (cond.status != ExprStatus::Ok) {
        return BodyResult{cond.status == ExprStatus::LineBreach ? BodyStatus::LineBreach
                                                                : BodyStatus::Gap,
                          {}, std::move(cond.detail)};
    }
    bool take = false;
    if (!asBool(cond.value, &take))
        return BodyResult{BodyStatus::Gap, {}, "'if' needs a bool condition"};
    if (take) return evaluateBody(*node.then_block, env);
    if (!node.else_stmt) return BodyResult{BodyStatus::Empty, {}, {}};
    if (const auto* elseBlock = dynamic_cast<const Block*>(node.else_stmt.get()))
        return evaluateBody(*elseBlock, env);
    if (const auto* elseIf = dynamic_cast<const IfStatement*>(node.else_stmt.get()))
        return evaluateIf(*elseIf, env);
    return BodyResult{BodyStatus::Gap, {}, "an 'else' holds a block or an 'if'"};
}

} // namespace fin::comptime
