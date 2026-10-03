#include "ComptimeInterp.hpp"

#include <cstdint>
#include <functional>
#include <limits>

#include "CompilerApi.hpp"
#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/FunctionDecl.hpp"
#include "../ast/decls/DefineDecl.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/StructDecl.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/ArrayExpr.hpp"
#include "../ast/exprs/BinaryOp.hpp"
#include "../ast/exprs/FunctionCall.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/exprs/Lambda.hpp"
#include "../ast/exprs/Literal.hpp"
#include "../ast/exprs/MiscExpr.hpp"
#include "../ast/exprs/StructureExpr.hpp"
#include "../ast/exprs/UnaryOp.hpp"
#include "../ast/nodes/Parameter.hpp"
#include "../ast/stmts/ControlFlow.hpp"
#include "../ast/stmts/ErrorHandling.hpp"
#include "../ast/stmts/Statement.hpp"
#include "../ast/stmts/VariableDecl.hpp"

// The comptime value model, second step (see the header for the contract).
//
// Straight-line threading plus the closed operator list: `==`/`!=` on
// Int/Bool/String pairs, `!` on Bool, `&&`/`||` short-circuiting on Bool pairs, int
// comparison and int arithmetic, and G4's call-site forms (struct/array
// construction with member/index reads, identity casts, struct method calls,
// static component answers). Literals evaluate, lets bind, identifiers
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

// A composite value's kind, for diagnostics. Lowercase like the type
// spellings a program writes (`int`, `string`), not the enum names.
const char* valueKindName(ValueKind kind) {
    switch (kind) {
        case ValueKind::Int: return "int";
        case ValueKind::String: return "string";
        case ValueKind::Bool: return "bool";
        case ValueKind::Quote: return "quote";
        case ValueKind::Opaque: return "opaque";
        case ValueKind::Struct: return "struct";
        case ValueKind::Array: return "array";
    }
    return "unknown";
}

// A string literal's meaning: the lexeme with quotes stripped and escapes
// decoded, exactly the bytes the backend lowers (the same table as
// decodeLiteral in CodeGen_LLVM.cpp), so a folded `==` agrees with the
// runtime value — `"a\tb"` and a raw-tab spelling compare equal in both.
// Decoded once here: handler parameters already bind decoded text
// (EventFiring binds the identifier name), so every String value in the
// model holds content and `==` is a plain comparison. A local mirror, not a
// shared helper: the decoder it mirrors lives in codegen, which semantics
// must not include.
std::string decodeStringLiteral(const std::string& lexeme) {
    std::string body = lexeme;
    if (body.size() >= 2 && (body.front() == '"' || body.front() == '\'') &&
        body.back() == body.front()) {
        body = body.substr(1, body.size() - 2);
    }
    std::string out;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) {
            out += body[i];
            continue;
        }
        const char c = body[++i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '0': out += '\0'; break;
            case '\\': out += '\\'; break;
            case '\'': out += '\''; break;
            case '"': out += '"'; break;
            default: out += c; break;
        }
    }
    return out;
}

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

// Whether `obj` is `compiler.components.<comp>`: the reference a component
// call is made through. Anything else rooted at `compiler` (a use like
// `compiler.layout.size_of(t)`) is a call this model does not run, and
// anything not rooted at `compiler` is not a component call at all.
bool asComponentsRef(const Expression& obj, std::string* compOut) {
    const auto* outer = dynamic_cast<const MemberAccess*>(&obj);
    if (!outer || !outer->object) return false;
    const auto* inner = dynamic_cast<const MemberAccess*>(outer->object.get());
    if (!inner || !inner->object || inner->member != "components") return false;
    const auto* root = dynamic_cast<const Identifier*>(inner->object.get());
    if (!root || root->name != "compiler") return false;
    if (compOut) *compOut = outer->member;
    return true;
}

// Stage A: whether `expr` names a type for `@implements`/`compiler.types.
// implements`. A bare `Identifier` is the only foldable shape: it is the
// type's name, never a value, so the environment is not consulted (a
// `$struct`/`$interface` parameter of the same spelling is a runtime word
// and stays a gap for the Stage-C chain). Anything else -- a literal, a
// call, member access -- is a gap naming the query.
bool asImplementsTypeName(const Expression* expr, std::string* out) {
    if (!expr) return false;
    if (const auto* id = dynamic_cast<const Identifier*>(expr)) {
        if (out) *out = id->name;
        return true;
    }
    return false;
}

// Whether `call` is `compiler.types.implements(...)`: the object chain is
// exactly `compiler` then `types`. Mirrors the `defined` check in
// evaluateMethod below, answered the same way (hook first, gap otherwise).
bool asTypesImplementsCall(const MethodCall& call) {
    const auto* mem = dynamic_cast<const MemberAccess*>(call.object.get());
    if (!mem || !mem->object || mem->member != "types") return false;
    const auto* root = dynamic_cast<const Identifier*>(mem->object.get());
    return root && root->name == "compiler";
}

// Whether the expression is rooted at the `compiler` name: a member chain,
// a call on one, or the name itself. Component reads are answered (or named)
// by the evaluator, never by the environment — `compiler` is never bound.
bool isCompilerRooted(const Expression& expr) {
    const Expression* cur = &expr;
    while (true) {
        if (const auto* id = dynamic_cast<const Identifier*>(cur)) return id->name == "compiler";
        if (const auto* member = dynamic_cast<const MemberAccess*>(cur)) {
            if (!member->object) return false;
            cur = member->object.get();
            continue;
        }
        if (const auto* call = dynamic_cast<const MethodCall*>(cur)) {
            if (!call->object) return false;
            cur = call->object.get();
            continue;
        }
        return false;
    }
}

// Host taint (ADR 0017, Q11): any op on a tainted value stays tainted, naming
// the first host read it derived from. First op wins: one branch names one
// operation, and the value still warns exactly once.
void inheritTaint(Value& dst, const Value& src) {
    if (src.host_tainted && !dst.host_tainted) {
        dst.host_tainted = true;
        dst.host_op = src.host_op;
    }
}

bool isHostReadOpName(const std::string& method) {
    return method == "get_total_memory" || method == "get_available_memory" ||
           method == "get_memorycard_model";
}

// Whether `call` is `compiler.system.get_*(...)`: the object chain is exactly
// `compiler` then `system`, the method one of the host reads above. Mirrors
// the analyzer's syntactic check, answered here as a tainted value instead of
// a gap so call-through taint folds.
bool asHostSystemRead(const MethodCall& call, std::string* opOut) {
    if (!isHostReadOpName(call.method_name)) return false;
    const auto* sys = dynamic_cast<const MemberAccess*>(call.object.get());
    if (!sys || !sys->object || sys->member != "system") return false;
    const auto* root = dynamic_cast<const Identifier*>(sys->object.get());
    if (!root || root->name != "compiler") return false;
    if (opOut) *opOut = "compiler.system." + call.method_name;
    return true;
}

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

const StructDeclaration* Interpreter::findStruct(const std::string& name) const {
    for (const auto& stmt : program_.statements) {
        if (const auto* decl = dynamic_cast<const StructDeclaration*>(stmt.get())) {
            if (decl->name == name) return decl;
        }
    }
    return nullptr;
}

// B4: whether the program itself declares the name at top level (the
// analyzer hook's fallback). A lifted `@define` counts: a later guard in the
// same body sees the earlier taken declaration.
bool Interpreter::programDefines(const std::string& name) const {
    for (const auto& lifted : lifted_) {
        if (lifted == name) return true;
    }
    for (const auto& stmt : program_.statements) {
        if (const auto* fun = dynamic_cast<const FunctionDeclaration*>(stmt.get())) {
            if (fun->name == name) return true;
        } else if (const auto* def = dynamic_cast<const DefineDeclaration*>(stmt.get())) {
            if (def->name == name) return true;
        } else if (const auto* special = dynamic_cast<const SpecialDeclaration*>(stmt.get())) {
            if (special->name == name) return true;
        } else if (const auto* decl = dynamic_cast<const StructDeclaration*>(stmt.get())) {
            if (decl->name == name) return true;
        } else if (const auto* iface = dynamic_cast<const InterfaceDeclaration*>(stmt.get())) {
            if (iface->name == name) return true;
        } else if (const auto* en = dynamic_cast<const EnumDeclaration*>(stmt.get())) {
            if (en->name == name) return true;
        } else if (const auto* td = dynamic_cast<const TypeDefinition*>(stmt.get())) {
            if (td->name == name) return true;
        } else if (const auto* vd = dynamic_cast<const VariableDeclaration*>(stmt.get())) {
            if (vd->name == name) return true;
        }
    }
    return false;
}

std::optional<bool> Interpreter::answerDefined(const std::string& name) const {
    if (definedHook_) {
        if (auto hooked = definedHook_(name)) return hooked;
    }
    return programDefines(name);
}

ExprResult Interpreter::evaluateStruct(const StructInstantiation& node, const Env& env) {
    // The name first, then the fields: a mistake about which struct this is
    // outranks a mistake inside a field. A `$struct` *value* (`st` in `st{}`)
    // binds its handle's struct and constructs that, with explicit fields
    // evaluated and unlisted defaults filled closed. A bound-but-unhandled
    // name keeps the wave-4 gap; an unbound unknown name stays unknown.
    const StructDeclaration* decl = findStruct(node.struct_name);
    std::string actualName = node.struct_name;
    if (!decl) {
        const Value* bound = env.lookup(node.struct_name);
        if (bound && bound->kind == ValueKind::Opaque) {
            const std::string prefix = "$struct:";
            if (bound->text.rfind(prefix, 0) == 0) {
                actualName = bound->text.substr(prefix.size());
                decl = findStruct(actualName);
                if (!decl)
                    return ExprResult{ExprStatus::Gap, {}, "unknown struct '" + actualName + "'"};
            } else {
                return ExprResult{ExprStatus::Gap, {},
                                  "instantiating '$struct' value '" + node.struct_name +
                                      "' needs its type known at compile time "
                                      "(the compiler API's wave-4 value handle)"};
            }
        } else if (bound) {
            return ExprResult{ExprStatus::Gap, {},
                              "instantiating '$struct' value '" + node.struct_name +
                                  "' needs its type known at compile time "
                                  "(the compiler API's wave-4 value handle)"};
        } else {
            return ExprResult{ExprStatus::Gap, {}, "unknown struct '" + node.struct_name + "'"};
        }
    }
    std::vector<std::pair<std::string, Value>> fields;
    fields.reserve(node.fields.size());
    for (const auto& [fieldName, fieldExpr] : node.fields) {
        if (!fieldExpr)
            return ExprResult{ExprStatus::Gap, {},
                              "missing value for field '" + fieldName + "' in '" +
                                  actualName + "{...}'"};
        ExprResult field = evaluateExpression(*fieldExpr, env);
        if (field.status != ExprStatus::Ok) return field;
        // Last wins on a duplicate: the analyzer owns the diagnostic, so
        // evaluation never reports one.
        bool replaced = false;
        for (auto& [name, value] : fields) {
            if (name == fieldName) {
                value = field.value;
                replaced = true;
                break;
            }
        }
        if (!replaced) fields.emplace_back(fieldName, std::move(field.value));
    }
    // Declared defaults fill the unlisted members, evaluated closed: a
    // default is struct scope, not caller scope, so a caller local of the
    // same name must not leak in. A default that is not closed is a gap for
    // the whole construction — there is no value to claim.
    for (const auto& member : decl->members) {
        if (!member || !member->default_value) continue;
        bool present = false;
        for (const auto& [name, value] : fields) {
            if (name == member->name) {
                present = true;
                break;
            }
        }
        if (present) continue;
        Env closed;
        ExprResult filled = evaluateExpression(*member->default_value, closed);
        if (filled.status != ExprStatus::Ok) return filled;
        fields.emplace_back(member->name, std::move(filled.value));
    }
    Value built = Value::makeStruct(actualName, std::move(fields));
    for (const auto& [name, value] : built.fields) inheritTaint(built, value);
    return ExprResult{ExprStatus::Ok, std::move(built), {}};
}

ExprResult Interpreter::evaluateMember(const MemberAccess& node, const Env& env) {
    // A `compiler....` read is not a value: component calls answer through
    // evaluateMethod, and a bare read names that rather than reporting
    // "unknown name 'compiler'".
    if (node.object && isCompilerRooted(*node.object))
        return ExprResult{ExprStatus::Gap, {},
                          "cannot read 'compiler...' as a value at comptime "
                          "(component calls evaluate, member reads do not)"};
    if (!node.object)
        return ExprResult{ExprStatus::Gap, {}, "missing object in member access"};
    ExprResult object = evaluateExpression(*node.object, env);
    if (object.status != ExprStatus::Ok) return object;
    if (object.value.kind == ValueKind::Struct) {
        for (const auto& [name, value] : object.value.fields) {
            if (name == node.member) return ExprResult{ExprStatus::Ok, value, {}};
        }
        return ExprResult{ExprStatus::Gap, {},
                          "struct '" + object.value.struct_name + "' has no field '" +
                              node.member + "'"};
    }
    // The one synthetic read the analyzer owns too (`a.length` is an int):
    // anything else on an array is an index, not a member.
    if (object.value.kind == ValueKind::Array && node.member == "length")
        return ExprResult{ExprStatus::Ok,
                          Value::makeInt(spellInt(static_cast<std::int64_t>(
                              object.value.elements.size()))),
                          {}};
    if (object.value.kind == ValueKind::Opaque)
        return ExprResult{ExprStatus::Gap, {},
                          "member '" + node.member + "' of opaque '" + object.value.text +
                              "' is not known at comptime"};
    return ExprResult{ExprStatus::Gap, {},
                      "cannot read member '" + node.member + "' of a " +
                          valueKindName(object.value.kind) + " value"};
}

ExprResult Interpreter::evaluateArray(const ArrayLiteral& node, const Env& env) {
    std::vector<Value> elements;
    elements.reserve(node.elements.size());
    for (const auto& element : node.elements) {
        if (!element) return ExprResult{ExprStatus::Gap, {}, "missing element in array literal"};
        ExprResult evaluated = evaluateExpression(*element, env);
        if (evaluated.status != ExprStatus::Ok) return evaluated;
        elements.push_back(std::move(evaluated.value));
    }
    Value built = Value::makeArray(std::move(elements));
    for (const auto& element : built.elements) inheritTaint(built, element);
    return ExprResult{ExprStatus::Ok, std::move(built), {}};
}

ExprResult Interpreter::evaluateIndex(const ArrayAccess& node, const Env& env) {
    if (!node.array || !node.index)
        return ExprResult{ExprStatus::Gap, {}, "missing operand in index expression"};
    ExprResult array = evaluateExpression(*node.array, env);
    if (array.status != ExprStatus::Ok) return array;
    if (array.value.kind != ValueKind::Array) {
        if (array.value.kind == ValueKind::Opaque)
            return ExprResult{ExprStatus::Gap, {},
                              "index of opaque '" + array.value.text +
                                  "' is not known at comptime"};
        return ExprResult{ExprStatus::Gap, {},
                          "cannot index into a " + std::string(valueKindName(array.value.kind)) +
                              " value"};
    }
    ExprResult index = evaluateExpression(*node.index, env);
    if (index.status != ExprStatus::Ok) return index;
    std::int64_t at = 0;
    if (!asInt(index.value, &at))
        return ExprResult{ExprStatus::Gap, {}, "an index needs an int"};
    if (at < 0)
        return ExprResult{ExprStatus::Gap, {},
                          "negative index " + spellInt(at) + " is not evaluated at comptime"};
    if (static_cast<std::size_t>(at) >= array.value.elements.size())
        return ExprResult{ExprStatus::Gap, {},
                          "index " + spellInt(at) + " is out of range for an array of " +
                              std::to_string(array.value.elements.size())};
    Value picked = array.value.elements[static_cast<std::size_t>(at)];
    // The selection depends on the host when the index does.
    inheritTaint(picked, index.value);
    return ExprResult{ExprStatus::Ok, std::move(picked), {}};
}

ExprResult Interpreter::evaluateCast(const CastExpression& node, const Env& env) {
    if (!node.expr) return ExprResult{ExprStatus::Gap, {}, "missing operand in cast"};
    ExprResult inner = evaluateExpression(*node.expr, env);
    if (inner.status != ExprStatus::Ok) return inner;
    // A cast claims a type, never a value: what is unknown stays unknown
    // under the same identity, so `cast<T>(s)` threads exactly like `s`.
    if (inner.value.kind == ValueKind::Opaque) return inner;
    const bool simple = node.target_type && node.target_type->generics.empty() &&
                        node.target_type->pointer_depth == 0 && !node.target_type->is_array &&
                        !node.target_type->is_nullable;
    const std::string& target = node.target_type ? node.target_type->name : "";
    if (simple) {
        if (target == "int" && inner.value.kind == ValueKind::Int) return inner;
        if (target == "bool" && inner.value.kind == ValueKind::Bool) return inner;
        if (target == "string" && inner.value.kind == ValueKind::String) return inner;
    }
    return ExprResult{ExprStatus::Gap, {},
                      "cast to " + (target.empty() ? "a complex type" : "'" + target + "'") +
                          " is not evaluated at comptime"};
}

ExprResult Interpreter::evaluateMethod(const MethodCall& node, const Env& env) {
    // The static reference ops: asking *about* a component never needs a
    // grant (the gate Analyzer_CompilerApi.cpp holds), so the answers come
    // from the table, host-independent. `granted` needs the caller's grants,
    // which evaluation does not carry; every use (`compiler.<c>.<op>`) needs
    // layout or type facts this model does not hold.
    std::string component;
    if (node.object && asComponentsRef(*node.object, &component)) {
        if (node.args.empty()) {
            if (node.method_name == "present")
                return ExprResult{ExprStatus::Ok,
                                  Value::makeBool(compilerapi::findComponent(component)
                                                      ? "true"
                                                      : "false"),
                                  {}};
            if (node.method_name == "version") {
                const auto* found = compilerapi::findComponent(component);
                if (!found)
                    return ExprResult{ExprStatus::Gap, {},
                                      "there is no component '" + component + "'"};
                return ExprResult{ExprStatus::Ok, Value::makeInt(std::to_string(found->version)),
                                  {}};
            }
            if (node.method_name == "name")
                return ExprResult{ExprStatus::Ok, Value::makeString(component), {}};
            if (node.method_name == "granted")
                return ExprResult{ExprStatus::Gap, {},
                                  "'granted' needs the caller's grants, which comptime "
                                  "evaluation does not carry"};
        }
        return ExprResult{ExprStatus::Gap, {},
                          "component call 'compiler.components." + component + "." +
                              node.method_name + "' is not evaluated at comptime"};
    }
    // A host read folds to a placeholder of its kind, tainted with the
    // operation: the value is a stand-in (the branch, not the bytes, is what
    // the warning owns), and the taint is what threads through helpers. Args
    // are not evaluated: `compiler.enums.InBytes` is itself a gap, and the
    // taint comes from the read, not its arguments.
    {
        std::string hostOp;
        if (node.object && asHostSystemRead(node, &hostOp)) {
            Value folded = (node.method_name == "get_memorycard_model")
                               ? Value::makeString("")
                               : Value::makeInt("0");
            folded.host_tainted = true;
            folded.host_op = hostOp;
            return ExprResult{ExprStatus::Ok, std::move(folded), {}};
        }
    }
    // B4: `compiler.symbols.defined("name")` folds like `@defined` (ADR 0042).
    if (node.method_name == "defined" && node.object) {
        const auto* mem = dynamic_cast<const MemberAccess*>(node.object.get());
        const Identifier* root = nullptr;
        if (mem && mem->member == "symbols" && mem->object)
            root = dynamic_cast<const Identifier*>(mem->object.get());
        if (root && root->name == "compiler") {
            if (node.args.size() != 1)
                return ExprResult{ExprStatus::Gap, {},
                                  "'defined' expects 1 argument, got " +
                                      std::to_string(node.args.size())};
            std::string target;
            if (!node.args[0])
                return ExprResult{ExprStatus::Gap, {}, "missing argument in call 'defined'"};
            if (const auto* lit = dynamic_cast<const Literal*>(node.args[0].get())) {
                if (lit->kind != ASTTokenKind::STRING_LITERAL)
                    return ExprResult{ExprStatus::Gap, {},
                                      "Argument to 'defined' must be a string"};
                target = decodeStringLiteral(lit->value);
            } else {
                ExprResult arg = evaluateExpression(*node.args[0], env);
                if (arg.status != ExprStatus::Ok) return arg;
                if (arg.value.kind != ValueKind::String)
                    return ExprResult{ExprStatus::Gap, {},
                                      "Argument to 'defined' must be a string"};
                target = arg.value.text;
            }
            std::optional<bool> ans = answerDefined(target);
            if (!ans) return ExprResult{ExprStatus::Gap, {}, "unknown name '" + target + "'"};
            return ExprResult{ExprStatus::Ok, Value::makeBool(*ans ? "true" : "false"), {}};
        }
    }
    // Stage A: `compiler.types.implements(S, I)` folds through the same hook
    // as `@implements(S, I)` above -- one predicate behind both spellings.
    // Grant checking is the analyzer's (this model never carries grants);
    // unknown names stay a gap exactly as there.
    if (node.method_name == "implements" && node.object && asTypesImplementsCall(node)) {
        if (node.args.size() != 2)
            return ExprResult{ExprStatus::Gap, {},
                              "'compiler.types.implements' expects 2 arguments, got " +
                                  std::to_string(node.args.size())};
        std::string sName;
        std::string iName;
        if (!asImplementsTypeName(node.args[0].get(), &sName) ||
            !asImplementsTypeName(node.args[1].get(), &iName))
            return ExprResult{ExprStatus::Gap, {},
                              "'compiler.types.implements' needs type names at comptime "
                              "(a $struct/$interface value lowers through the runtime chain)"};
        if (!implementsHook_)
            return ExprResult{ExprStatus::Gap, {},
                              "unknown 'compiler.types.implements' pair '" + sName + ", " +
                                  iName + "' (no implements hook)"};
        std::optional<bool> ans = implementsHook_(sName, iName);
        if (!ans)
            return ExprResult{ExprStatus::Gap, {},
                              "unknown 'compiler.types.implements' pair '" + sName + ", " +
                                  iName + "'"};
        return ExprResult{ExprStatus::Ok, Value::makeBool(*ans ? "true" : "false"), {}};
    }
    if (node.object && isCompilerRooted(*node.object))
        return ExprResult{ExprStatus::Gap, {},
                          "component call 'compiler...' is not evaluated at comptime"};
    if (!node.object)
        return ExprResult{ExprStatus::Gap, {}, "missing receiver in method call"};
    ExprResult receiver = evaluateExpression(*node.object, env);
    if (receiver.status != ExprStatus::Ok) return receiver;
    if (receiver.value.kind != ValueKind::Struct) {
        if (receiver.value.kind == ValueKind::Opaque)
            return ExprResult{ExprStatus::Gap, {},
                              "method '" + node.method_name + "' of opaque '" +
                                  receiver.value.text + "' is not known at comptime"};
        return ExprResult{ExprStatus::Gap, {},
                          "cannot call method '" + node.method_name + "' on a " +
                              std::string(valueKindName(receiver.value.kind)) + " value"};
    }
    const StructDeclaration* decl = findStruct(receiver.value.struct_name);
    if (!decl)
        return ExprResult{ExprStatus::Gap, {},
                          "unknown struct '" + receiver.value.struct_name + "'"};
    const FunctionDeclaration* method = nullptr;
    for (const auto& candidate : decl->methods) {
        if (candidate && candidate->name == node.method_name) {
            method = candidate.get();
            break;
        }
    }
    if (!method)
        return ExprResult{ExprStatus::Gap, {},
                          "struct '" + receiver.value.struct_name + "' has no method '" +
                              node.method_name + "'"};
    if (!method->body)
        return ExprResult{ExprStatus::Gap, {},
                          "method '" + receiver.value.struct_name + "." + node.method_name +
                              "' has no body to run"};
    std::vector<Value> argValues;
    argValues.reserve(node.args.size());
    for (const auto& arg : node.args) {
        if (!arg)
            return ExprResult{ExprStatus::Gap, {},
                              "missing argument in call '" + node.method_name + "'"};
        ExprResult evaluated = evaluateExpression(*arg, env);
        if (evaluated.status != ExprStatus::Ok) return evaluated;
        argValues.push_back(std::move(evaluated.value));
    }
    // The no-hang guarantee, keyed `Struct.method`: the same depth bound and
    // cycle naming as a free call, so a method that calls itself is the same
    // named gap rather than a hang.
    const std::string key = receiver.value.struct_name + "." + node.method_name;
    if (activeCalls_.count(key) != 0) {
        std::string chain;
        bool recording = false;
        for (const auto& frame : callStack_) {
            if (frame == key) recording = true;
            if (recording) {
                if (!chain.empty()) chain += " -> ";
                chain += frame;
            }
        }
        chain += " -> " + key;
        return ExprResult{ExprStatus::Gap, {},
                          "recursive call '" + key + "' in cycle '" + chain + "'"};
    }
    if (depth_ >= kMaxDepth)
        return ExprResult{ExprStatus::Gap, {}, "recursive call '" + key + "'"};
    // A written `self` takes the receiver and the rest align behind it; an
    // unwritten one is the injected implicit self, bound anyway so a body
    // that names it still reads the receiver.
    const bool takesSelf =
        !method->params.empty() && method->params[0]->name == "self";
    if (argValues.size() + (takesSelf ? 1 : 0) != method->params.size())
        return ExprResult{ExprStatus::Gap, {}, "wrong arity for '" + key + "'"};
    Env calleeEnv;
    calleeEnv.bind("self", receiver.value);
    for (std::size_t i = 0; i < argValues.size(); ++i)
        calleeEnv.bind(method->params[i + (takesSelf ? 1 : 0)]->name, std::move(argValues[i]));
    activeCalls_.insert(key);
    callStack_.push_back(key);
    ++depth_;
    BodyResult ran = evaluateBody(*method->body, calleeEnv);
    --depth_;
    callStack_.pop_back();
    activeCalls_.erase(key);
    switch (ran.status) {
        case BodyStatus::Returned: return ExprResult{ExprStatus::Ok, std::move(ran.value), {}};
        case BodyStatus::LineBreach: return ExprResult{ExprStatus::LineBreach, {}, ran.detail};
        case BodyStatus::Blame:
            return ExprResult{ExprStatus::Gap, {}, "blame in '" + key + "'"};
        case BodyStatus::Empty:
            return ExprResult{ExprStatus::Gap, {}, "no return in '" + key + "'"};
        case BodyStatus::Gap: return ExprResult{ExprStatus::Gap, {}, ran.detail};
    }
    return ExprResult{ExprStatus::Gap, {}, "call '" + key + "' did not evaluate"};
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
                return ExprResult{ExprStatus::Ok, Value::makeString(decodeStringLiteral(lit->value)), {}};
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
    if (const auto* call = dynamic_cast<const FunctionCall*>(&expr)) {
        // B4 (ADR 0042): `@defined("name")` folds to whether the name is
        // declared (hook = analyzer scopes with the eager ambient, else the
        // program's own top level + lifted). Missing means false, never a
        // gap; only a bad shape stays a gap.
        if (call->is_special && call->name == "defined") {
            if (call->args.size() != 1)
                return ExprResult{ExprStatus::Gap, {},
                                  "'@defined' expects 1 argument, got " +
                                      std::to_string(call->args.size())};
            std::string target;
            if (const auto* lit = dynamic_cast<const Literal*>(call->args[0].get())) {
                if (lit->kind != ASTTokenKind::STRING_LITERAL)
                    return ExprResult{ExprStatus::Gap, {},
                                      "Argument to '@defined' must be a string"};
                target = decodeStringLiteral(lit->value);
            } else {
                ExprResult arg = evaluateExpression(*call->args[0], env);
                if (arg.status != ExprStatus::Ok) return arg;
                if (arg.value.kind != ValueKind::String)
                    return ExprResult{ExprStatus::Gap, {},
                                      "Argument to '@defined' must be a string"};
                target = arg.value.text;
            }
            std::optional<bool> ans = answerDefined(target);
            if (!ans) return ExprResult{ExprStatus::Gap, {}, "unknown name '" + target + "'"};
            return ExprResult{ExprStatus::Ok, Value::makeBool(*ans ? "true" : "false"), {}};
        }
        // Stage A (Wave-4 `@implements` lowering): `@implements(S, I)` folds
        // through the compiler-API op -- the same hook `compiler.types.
        // implements` answers below (the `symbols.defined` precedent: one
        // predicate behind both spellings). Only bare type names fold; a
        // `$struct`/`$interface` parameter is a runtime tid word and stays a
        // gap for the Stage-C chain. No hook, or names the hook does not
        // know, is a gap naming the query, never a guessed false.
        if (call->is_special && call->name == "implements") {
            if (call->args.size() != 2)
                return ExprResult{ExprStatus::Gap, {},
                                  "'@implements' expects 2 arguments, got " +
                                      std::to_string(call->args.size())};
            std::string sName;
            std::string iName;
            if (!asImplementsTypeName(call->args[0].get(), &sName) ||
                !asImplementsTypeName(call->args[1].get(), &iName))
                return ExprResult{ExprStatus::Gap, {},
                                  "'@implements' needs type names at comptime "
                                  "(a $struct/$interface value lowers through "
                                  "the runtime chain)"};
            if (!implementsHook_)
                return ExprResult{ExprStatus::Gap, {},
                                  "unknown '@implements' pair '" + sName + ", " + iName +
                                      "' (no implements hook)"};
            std::optional<bool> ans = implementsHook_(sName, iName);
            if (!ans)
                return ExprResult{ExprStatus::Gap, {},
                                  "unknown '@implements' pair '" + sName + ", " + iName + "'"};
            return ExprResult{ExprStatus::Ok, Value::makeBool(*ans ? "true" : "false"), {}};
        }
        return evaluateCall(call->name, call->args, env);
    }
    if (const auto* inv = dynamic_cast<const MacroInvocation*>(&expr))
        return evaluateCall(inv->name, inv->args, env);
    // G4: the call-site forms. Static and qualified method shapes outside
    // these (a `Type::method` static call, `new`, enum construction) keep
    // the node-kind gap below.
    if (const auto* constructed = dynamic_cast<const StructInstantiation*>(&expr))
        return evaluateStruct(*constructed, env);
    if (const auto* member = dynamic_cast<const MemberAccess*>(&expr))
        return evaluateMember(*member, env);
    if (const auto* array = dynamic_cast<const ArrayLiteral*>(&expr))
        return evaluateArray(*array, env);
    if (const auto* index = dynamic_cast<const ArrayAccess*>(&expr))
        return evaluateIndex(*index, env);
    if (const auto* cast = dynamic_cast<const CastExpression*>(&expr))
        return evaluateCast(*cast, env);
    if (const auto* method = dynamic_cast<const MethodCall*>(&expr))
        return evaluateMethod(*method, env);
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
        ExprResult arm = evaluateExpression(take ? *tern->true_expr : *tern->false_expr, env);
        if (arm.status != ExprStatus::Ok) return arm;
        // The choice is host-dependent when the condition is.
        inheritTaint(arm.value, cond.value);
        return arm;
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
        Value folded = Value::makeBool(spellBool(!b));
        inheritTaint(folded, inner.value);
        return ExprResult{ExprStatus::Ok, std::move(folded), {}};
    }
    std::int64_t n = 0;
    if (!asInt(inner.value, &n))
        return ExprResult{ExprStatus::Gap, {}, "unary '-' needs an int"};
    // Wrapping negation, exactly the backend's `sub 0, n`: INT64_MIN stays
    // INT64_MIN rather than trapping, so there is nothing to refuse.
    const std::uint64_t negated = 0 - static_cast<std::uint64_t>(n);
    Value folded = Value::makeInt(spellInt(static_cast<std::int64_t>(negated)));
    inheritTaint(folded, inner.value);
    return ExprResult{ExprStatus::Ok, std::move(folded), {}};
}

ExprResult Interpreter::evaluateBinary(const BinaryOp& node, const Env& env) {
    // S1, closed: `==`/`!=` on Int/Bool/String pairs, `&&`/`||` short-circuiting
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
        if (node.op == ASTTokenKind::AND && !leftBool) {
            Value folded = Value::makeBool("false");
            inheritTaint(folded, left.value);
            return ExprResult{ExprStatus::Ok, std::move(folded), {}};
        }
        if (node.op == ASTTokenKind::OR && leftBool) {
            Value folded = Value::makeBool("true");
            inheritTaint(folded, left.value);
            return ExprResult{ExprStatus::Ok, std::move(folded), {}};
        }
        ExprResult right = evaluateExpression(*node.right, env);
        if (right.status != ExprStatus::Ok) return right;
        bool rightBool = false;
        if (!asBool(right.value, &rightBool))
            return ExprResult{ExprStatus::Gap, {}, "'&&' and '||' need bools"};
        Value folded = Value::makeBool(spellBool(rightBool));
        inheritTaint(folded, left.value);
        inheritTaint(folded, right.value);
        return ExprResult{ExprStatus::Ok, std::move(folded), {}};
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
        } else if (left.value.kind == ValueKind::String && right.value.kind == ValueKind::String) {
            // I-G2: byte equality over decoded content, the runtime rule
            // (verified: distinct spellings of the same bytes compare equal
            // at runtime). Mixed kinds and opaque subjects stay a gap below.
            equal = (left.value.text == right.value.text);
        } else {
            return ExprResult{ExprStatus::Gap, {}, "'==' needs two ints, two bools or two strings"};
        }
        const bool result = (node.op == ASTTokenKind::EQEQ) ? equal : !equal;
        Value folded = Value::makeBool(spellBool(result));
        inheritTaint(folded, left.value);
        inheritTaint(folded, right.value);
        return ExprResult{ExprStatus::Ok, std::move(folded), {}};
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
        Value folded = Value::makeBool(spellBool(result));
        inheritTaint(folded, left.value);
        inheritTaint(folded, right.value);
        return ExprResult{ExprStatus::Ok, std::move(folded), {}};
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
        Value folded = Value::makeInt(spellInt(static_cast<std::int64_t>(raw)));
        inheritTaint(folded, left.value);
        inheritTaint(folded, right.value);
        return ExprResult{ExprStatus::Ok, std::move(folded), {}};
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

ExprStatus Interpreter::evaluateAssign(const BinaryOp& node, Env& env,
                                       std::string* detail) {
    // I-G1: only a plain `name = value` rebinds. Anything else is the same
    // named gap `evaluateBinary` has always reported for it ("BinaryOp",
    // "missing operand ..."), so a store through no model never evaluates.
    if (node.op != ASTTokenKind::EQUAL || !node.left || !node.right) {
        if (!node.left || !node.right) {
            if (detail) *detail = "missing operand in binary expression";
            return ExprStatus::Gap;
        }
        if (detail) *detail = std::string(nodeKindName(node.kind()));
        return ExprStatus::Gap;
    }
    const auto* target = dynamic_cast<const Identifier*>(node.left.get());
    if (!target) {
        if (detail) *detail = std::string(nodeKindName(node.kind()));
        return ExprStatus::Gap;
    }
    ExprResult rhs = evaluateExpression(*node.right, env);
    if (rhs.status != ExprStatus::Ok) {
        if (detail) *detail = std::move(rhs.detail);
        return rhs.status;
    }
    env.bind(target->name, std::move(rhs.value));
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
            // I-G1: a plain `name = value;` rebinds through evaluateAssign
            // (the right side still folds through the operator list, calls
            // still run under the depth and acyclicity guards). Any other
            // assignment shape keeps its named gap inside evaluateAssign;
            // any other expression keeps the discard path below.
            if (const auto* assign = dynamic_cast<const BinaryOp*>(exprStmt->expr.get())) {
                if (assign->op == ASTTokenKind::EQUAL) {
                    std::string detail;
                    const ExprStatus status = evaluateAssign(*assign, env, &detail);
                    if (status != ExprStatus::Ok) {
                        return BodyResult{status == ExprStatus::LineBreach
                                              ? BodyStatus::LineBreach
                                              : BodyStatus::Gap,
                                          {}, std::move(detail)};
                    }
                    continue;
                }
            }
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
            // B4: a gated (taken-arm) `@define` lifts its declaration and the
            // body continues; an ungated one keeps the Q5 refusal, never run
            // and never silently dropped.
            if (gatedDepth_ > 0) {
                lifted_.push_back(def->name);
                continue;
            }
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
    // Host taint (ADR 0017, Q11) rides the condition into the taken arm's
    // answer: which arm runs is host-dependent, so the answer is too, and a
    // helper returning it threads the taint to its caller.
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
    // B4: the taken arm is gated, so a `@define` inside it lifts (gatedDepth_
    // counts the enclosing taken guards; an `else if` dispatches without
    // gating here and gates its own taken arm inside the recursion).
    struct Gate {
        Interpreter* self;
        explicit Gate(Interpreter* s) : self(s) { ++self->gatedDepth_; }
        ~Gate() { --self->gatedDepth_; }
    };
    BodyResult arm{BodyStatus::Empty, {}, {}};
    if (take) {
        Gate g(this);
        arm = evaluateBody(*node.then_block, env);
    } else if (!node.else_stmt) return BodyResult{BodyStatus::Empty, {}, {}};
    else if (const auto* elseBlock = dynamic_cast<const Block*>(node.else_stmt.get())) {
        Gate g(this);
        arm = evaluateBody(*elseBlock, env);
    } else if (const auto* elseIf = dynamic_cast<const IfStatement*>(node.else_stmt.get()))
        arm = evaluateIf(*elseIf, env);
    else return BodyResult{BodyStatus::Gap, {}, "an 'else' holds a block or an 'if'"};
    if (arm.status == BodyStatus::Returned) inheritTaint(arm.value, cond.value);
    return arm;
}

} // namespace fin::comptime
