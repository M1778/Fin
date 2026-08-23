#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

namespace {

// Fin spells a negative constant as a UnaryOp over a Literal -- the lexer never
// produces a signed INTEGER token (parser.y:2139) -- so "is this an integer
// constant, and is it negative" is answerable from the syntax alone, with no
// evaluation and no constant folder.
//
// `1 + 1` is deliberately *not* a constant here. Whether an int-typed expression
// converts to an unsigned type is a language decision rather than a defect, and
// KnownDefect_IntegerConstants holds it open; folding arithmetic would answer it
// by accident for the subset that happens to be foldable.
bool integerConstant(const ASTNode& node, bool& negative) {
    if (auto* lit = dynamic_cast<const Literal*>(&node)) {
        negative = false;
        return lit->kind == ASTTokenKind::INTEGER;
    }
    if (auto* un = dynamic_cast<const UnaryOp*>(&node)) {
        if (un->op != ASTTokenKind::MINUS || !un->operand) return false;
        bool inner = false;
        if (!integerConstant(*un->operand, inner)) return false;
        negative = !inner;  // `--1` is non-negative; nesting costs nothing to allow
        return true;
    }
    return false;
}

// The three unsigned types Analyzer_Core registers. `char` is not among them:
// whether it is signed is undecided, so it accepts a negative constant rather
// than having this function invent the answer.
bool isUnsignedIntegerName(const std::string& n) {
    return n == "uint" || n == "ulong" || n == "ushort";
}

bool isSignedIntegerName(const std::string& n) {
    return n == "int" || n == "long" || n == "short" || n == "char";
}

bool isFloatingName(const std::string& n) {
    return n == "float" || n == "double";
}

} // namespace

bool SemanticAnalyzer::constantFitsType(const ASTNode& node, const Type& target) {
    bool negative = false;
    if (!integerConstant(node, negative)) return false;

    const auto* prim = target.as<PrimitiveType>();
    if (!prim) return false;

    // The magnitude is not checked, and that is a decision rather than an
    // oversight: Fin has not said how wide `short` or `char` is, and the `{N}`
    // annotation that would say is erased by resolveTypeFromAST before anything
    // can read it. A range check today would be inventing the widths.
    // KnownDefect_IntegerWidths.AConstantTooLargeForItsTargetIsAccepted records
    // the hole and is where the check goes when the widths become real.
    if (isFloatingName(prim->name)) return true;
    if (isSignedIntegerName(prim->name)) return true;
    if (isUnsignedIntegerName(prim->name)) return !negative;
    return false;  // bool, string, void, auto and every named type: unchanged
}

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& d, bool debug) 
    : diag(d), debugMode(debug) {
    
    // Create Global Scope
    globalScope = std::make_shared<Scope>(nullptr);
    currentScope = globalScope;
    scopeStack.push_back(globalScope);
    
    // Builtins
    currentScope->defineType("int", std::make_shared<PrimitiveType>("int"));
    currentScope->defineType("float", std::make_shared<PrimitiveType>("float"));
    currentScope->defineType("void", std::make_shared<PrimitiveType>("void"));
    currentScope->defineType("bool", std::make_shared<PrimitiveType>("bool"));
    currentScope->defineType("string", std::make_shared<PrimitiveType>("string"));
    currentScope->defineType("auto", std::make_shared<PrimitiveType>("auto"));
    
    // Extended Primitives
    currentScope->defineType("char", std::make_shared<PrimitiveType>("char"));
    currentScope->defineType("long", std::make_shared<PrimitiveType>("long"));
    currentScope->defineType("double", std::make_shared<PrimitiveType>("double"));
    currentScope->defineType("short", std::make_shared<PrimitiveType>("short"));
    currentScope->defineType("uint", std::make_shared<PrimitiveType>("uint"));
    currentScope->defineType("ulong", std::make_shared<PrimitiveType>("ulong"));
    currentScope->defineType("ushort", std::make_shared<PrimitiveType>("ushort"));
    
    // Mock Castable
    currentScope->defineType("Castable", std::make_shared<StructType>("Castable"));

    // Compile-time reflection meta-types. A value of one of these *is* a type
    // (or an enum member), which is why the corpus writes them in type position:
    // `fun cast<_Type: $type>(...)` with the comment "$type == literal type"
    // (stdlib/types.fin:33), `tftid(tid: uint) <$type>` "returns a type from
    // typeid" (:83), `keyidof(enum_member: $enum_member)` with the example
    // `keyidof(Ok)` (stdlib/enums.fin:22), and `compatible(iface: $interface,
    // struct_: $struct)` (literal_interface.fin:5).
    //
    // Four names, listed rather than matched on the `$` prefix. The grammar
    // accepts *any* `$name` as a type (parser.y:1783, `DOLLAR IDENTIFIER`), so a
    // prefix rule would turn every misspelling into a silently accepted type;
    // Soundness_MetaTypes.AnUnknownDollarNameIsStillUndefined forbids exactly that.
    //
    // PrimitiveType and not a new Type subclass, because PrimitiveType's
    // assignability is name equality plus the one int->float rule
    // (PrimitiveType.cpp:10-14), which gives these the behaviour they need today:
    // a `$type` is accepted where `$type` is asked for and nowhere else. What a
    // `$type` value can *do* -- be compared, be instantiated, be passed to
    // `compiler.types.*` -- is wave 4 and is not decided by registering the name.
    // Nothing treats "is a PrimitiveType" as "is a number": the numeric
    // predicates above are explicit allowlists.
    currentScope->defineType("$type", std::make_shared<PrimitiveType>("$type"));
    currentScope->defineType("$struct", std::make_shared<PrimitiveType>("$struct"));
    currentScope->defineType("$interface", std::make_shared<PrimitiveType>("$interface"));
    currentScope->defineType("$enum_member", std::make_shared<PrimitiveType>("$enum_member"));
}

SemanticAnalyzer::~SemanticAnalyzer() {}

void SemanticAnalyzer::enterScope() {
    auto newScope = std::make_shared<Scope>(currentScope.get());
    currentScope = newScope;
    scopeStack.push_back(newScope);
}

void SemanticAnalyzer::exitScope() {
    if (scopeStack.size() > 1) {
        scopeStack.pop_back();
        currentScope = scopeStack.back();
    }
}

// Returns nullptr when the type cannot be resolved, having already reported why.
//
// A composite branch must propagate a child's nullptr rather than wrapping it,
// because no part of the type layer is prepared for a null child: PointerType,
// ArrayType, FunctionType and PrototypeType all dereference theirs in
// toString(), which is the first thing any caller asks. Returning a non-null
// composite over a failed child also defeats every caller's `if (!type)` guard,
// so the failure travels silently until something crashes on it.
//
// Children are all resolved before the failure is returned, so that a type
// naming two undefined types reports both rather than only the first.
// tests/samples/nullifier.fin is the specification. parser.y sets `is_nullable`
// on a TypeNode in twenty places -- every nullable spelling the language has:
// `let x? <T>`, six struct-member forms, `n?: T`, and the return type node under
// `fun?` -- and until this wave nothing in src/semantics/ or src/types/ ever read
// it. Reading it here, once, is what gives all twenty a meaning, and it is why
// `fun?` needed no change of its own: the grammar already marks the return type.
std::shared_ptr<Type> SemanticAnalyzer::resolveTypeFromAST(TypeNode* node) {
    auto resolved = resolveTypeUnwrapped(node);
    // `!resolved` first: a null node resolves to null and has no flag to read.
    if (!resolved || !node->is_nullable) return resolved;
    return std::make_shared<NullableType>(resolved);
}

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeUnwrapped(TypeNode* node) {
    if (!node) return nullptr;
    
    // 1. Pointer Type
    if (auto* ptrNode = dynamic_cast<PointerTypeNode*>(node)) {
        auto inner = resolveTypeFromAST(ptrNode->pointee.get());
        if (!inner) return nullptr;
        return std::make_shared<PointerType>(inner);
    }

    // 2. Array Type (FIXED: Validate Size)
    if (auto* arrNode = dynamic_cast<ArrayTypeNode*>(node)) {
        auto inner = resolveTypeFromAST(arrNode->element_type.get());
        bool fixed = (arrNode->size != nullptr);
        
        if (fixed) {
            // Analyze the size expression
            arrNode->size->accept(*this);
            
            // Ensure it evaluates to an integer
            auto intType = currentScope->resolveType("int");
            if (lastExprType) {
                if (!checkType(*arrNode->size, lastExprType, intType)) {
                    error(*arrNode->size, "Array size must be an integer");
                }
            }
        }
        
        // After the size check, so that `[NoSuchType; wrongsize]` reports both.
        if (!inner) return nullptr;
        return std::make_shared<ArrayType>(inner, fixed);
    }

    // 3. Function Type
    if (auto* fnNode = dynamic_cast<FunctionTypeNode*>(node)) {
        // `fn<T: Castable>(m: T) -> T` (lambdas.fin:69) declares T for the
        // parameter and return types that follow, so those are resolved in a
        // scope that has it, the way visit(LambdaExpression&) does for the value
        // side of that same line. A non-generic fn type enters an empty scope,
        // which changes nothing about how its types resolve.
        enterScope();
        declareGenericParams(fnNode->generic_params);

        std::vector<std::shared_ptr<Type>> pTypes;
        bool resolved = true;
        for(auto& p : fnNode->param_types) {
            pTypes.push_back(resolveTypeFromAST(p.get()));
            if (!pTypes.back()) resolved = false;
        }
        auto rType = resolveTypeFromAST(fnNode->return_type.get());
        exitScope();

        if (!rType || !resolved) return nullptr;
        return std::make_shared<FunctionType>(pTypes, rType);
    }

    // 4. Base Type (Identifier)
    if (node->is_prototype) {
        std::shared_ptr<Type> keyType = currentScope->resolveType("any");
        std::shared_ptr<Type> valueType = currentScope->resolveType("any");
        
        if (!keyType) keyType = std::make_shared<PrimitiveType>("any");
        if (!valueType) valueType = std::make_shared<PrimitiveType>("any");

        if (node->generics.size() >= 1) {
            keyType = resolveTypeFromAST(node->generics[0].get());
        }
        if (node->generics.size() >= 2) {
            valueType = resolveTypeFromAST(node->generics[1].get());
        }
        
        if (!keyType || !valueType) return nullptr;
        return std::make_shared<PrototypeType>(keyType, valueType);
    }

    auto type = currentScope->resolveType(node->name);
    if (!type) {
        error(*node, "Undefined type '" + node->name + "'");
        return nullptr;
    }
    
    // 5. Generics
    if (!node->generics.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        auto structDef = std::dynamic_pointer_cast<StructType>(type);
        bool argsResolved = true;
        
        for(size_t i = 0; i < node->generics.size(); ++i) {
            auto argType = resolveTypeFromAST(node->generics[i].get());
            args.push_back(argType);
            if (!argType) { argsResolved = false; continue; }
            
            if (structDef && i < structDef->generic_args.size()) {
                auto genParam = std::dynamic_pointer_cast<GenericType>(structDef->generic_args[i]);
                if (genParam && genParam->constraint) {
                    checkConstraint(node->generics[i].get(), argType, genParam->constraint);
                }
            }
        }
        
        // Every argument was resolved first, so all the undefined ones are
        // reported; a constrained parameter given an unresolved argument is not
        // additionally reported as violating its constraint, since there is no
        // type there to have violated it.
        if (!argsResolved) return nullptr;
        
        if (structDef) {
             auto instantiated = structDef->instantiate(args);
             if (instantiated) type = instantiated;
             else error(*node, "Generic count mismatch");
        } else {
             type = std::make_shared<StructType>(node->name, args);
        }
    }
    
    if (type && !node->annotations.empty()) {
        for (auto& ann : node->annotations) {
            ann->accept(*this);
        }
    }

    return type;
}

void SemanticAnalyzer::declareGenericParams(
        const std::vector<std::unique_ptr<GenericParam>>& params,
        std::vector<std::shared_ptr<Type>>* collect) {
    // Pass 1: every name, so a constraint can name a sibling parameter or the
    // parameter it constrains.
    std::vector<std::shared_ptr<GenericType>> made;
    made.reserve(params.size());
    for (auto& gen : params) {
        auto genType = std::make_shared<GenericType>(gen->name);
        currentScope->defineType(gen->name, genType);
        made.push_back(genType);
        if (collect) collect->push_back(genType);
    }

    // Pass 2: the constraints. resolveTypeFromAST reports an unresolved one, which
    // is the whole difference at the function, interface and operator sites --
    // they never called it. The resolved constraint is then attached to the
    // GenericType rather than logged and dropped, which is what makes
    // checkConstraint (below) reachable: nothing in src/ assigned that field, so
    // its `if (genParam->constraint)` guard was permanently false and every
    // constraint in the language was decorative.
    for (size_t i = 0; i < params.size(); ++i) {
        if (!params[i]->constraint) continue;
        auto resolved = resolveTypeFromAST(params[i]->constraint.get());
        if (!resolved) continue;  // already reported; leave the parameter unconstrained
        made[i]->constraint = resolved;
        debugLog(fg(fmt::color::gray), "      [Constraint] Generic '{}' : '{}'\n",
                 params[i]->name, resolved->toString());
    }
}

bool SemanticAnalyzer::checkConstraint(TypeNode* typeNode, std::shared_ptr<Type> actualType, std::shared_ptr<Type> constraint) {
    if (!constraint) return true;

    if (auto* iface = dynamic_cast<StructType*>(constraint.get())) {
        if (auto* st = dynamic_cast<StructType*>(actualType.get())) {
            if (!st->implements(iface)) {
                error(*typeNode, fmt::format("Type '{}' does not implement interface '{}'", 
                    actualType->toString(), iface->toString()));
                return false;
            }
        }
    }
    return true;
}

void SemanticAnalyzer::error(ASTNode& node, const std::string& msg) {
    diag.reportError(node.loc, msg);
    hasError = true;
}

bool SemanticAnalyzer::checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected) {
    if (!actual || !expected) return false;
    
    if (!actual->isAssignableTo(*expected)) {
        if (constantFitsType(node, *expected)) return true;
        error(node, fmt::format("Type mismatch: expected '{}', got '{}'", expected->toString(), actual->toString()));
        return false;
    }
    return true;
}

// A declaration may be initialised to `null` whatever its declared type is.
//
// nullifier.fin:4 calls `b? <int>` "equavelant to `b <int> = null,`", which reads
// two ways: either `= null` makes the declaration nullable, or `null` is simply a
// permitted "absent" initialiser. Two normative samples settle it. deeptest4.fin:6
// writes `integer <int> = null` and line 16 then compares `a["Hi"].integer` with
// `10`; stdlib/error.fin:11 writes `err_code: int = null` and line 14 passes
// `err_code` straight into an `<int>` field. Neither denullifies. Under the first
// reading both would have to, so the second is the reading the corpus supports:
// the initialiser is permitted and the declared type is unchanged.
//
// Deliberately not folded into checkType, which is also the *assignment* check:
// `let x <int> = null;` is legal and `x = null;` on the next line is not.
bool SemanticAnalyzer::checkInitializer(ASTNode& node, std::shared_ptr<Type> actual,
                                       std::shared_ptr<Type> expected) {
    if (isNullLiteral(actual)) return true;
    return checkType(node, actual, expected);
}

void SemanticAnalyzer::visit(Parameter& node) {
    resolveTypeFromAST(node.type.get());
    if (node.default_value) node.default_value->accept(*this);
}

void SemanticAnalyzer::visit(StructMember& node) {
    resolveTypeFromAST(node.type.get());
    if (node.default_value) node.default_value->accept(*this);
}

void SemanticAnalyzer::visit(PointerTypeNode& node) { resolveTypeFromAST(&node); }
void SemanticAnalyzer::visit(ArrayTypeNode& node) { resolveTypeFromAST(&node); }


void SemanticAnalyzer::visit(Program& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
}

void SemanticAnalyzer::visit(TypeNode& node) { resolveTypeFromAST(&node); }
void SemanticAnalyzer::visit(FunctionTypeNode& node) { resolveTypeFromAST(&node); }

}