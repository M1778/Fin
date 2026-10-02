#include "../SemanticAnalyzer.hpp"
#include "../EventPayloads.hpp"
#include "../../ast/StructuralWalk.hpp"
#include "../../ast/types/Attribute.hpp"
#include "../../types/TypeImpl.hpp"
#include "../../utils/IntegerConstant.hpp"
#include "../../types/Layout.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

namespace {

// integerConstant and readConstant used to be defined here. They moved to
// src/utils/IntegerConstant.hpp when the backend started lowering fixed arrays,
// because both passes read the *same* constants and have to agree about them:
// this file decides `[int, 5]`'s extent and whether `a[7]` is inside it, and the
// backend decides how many elements to allocate and which one a GEP lands on. Two
// readers that agree today are two readers that disagree after one edit, and the
// disagreement is a program that compiles and indexes past its own array.
//
// The local alias keeps this file's call sites reading as they did.
using ExtentRead = ConstantRead;
constexpr auto readExtent = readConstant;

// The unsigned integer spellings the analyzer registers. `char` is not among
// them: whether it is signed is undecided, so it accepts a negative constant
// rather than having this function invent the answer.
bool isUnsignedIntegerName(const std::string& n) {
    return n == "uint" || n == "ulong" || n == "ushort" ||
           n == "u8" || n == "u16" || n == "u32" || n == "u64" ||
           n == "usize" || n == "size_t";
}

bool isSignedIntegerName(const std::string& n) {
    return n == "int" || n == "long" || n == "short" || n == "char";
}

bool isFloatingName(const std::string& n) {
    return n == "float" || n == "double";
}

// `Self<T>` written inside a declaration of `X<T>` names the type being declared,
// not an instantiation of it: the arguments repeat the parameters the header
// declared three characters earlier. stdlib/stdptr.fin writes it four times inside
// `interface rptr_iface<T>` -- on 16, 22, 23 and 32 -- and every one reported
// `Generic count mismatch`.
//
// Answering "yes" here means the caller hands back the type it already resolved,
// which for an interface is the live interface rather than a copy of it. That
// matters more than it looks: the copy would be taken at the line `Self<T>` is
// written on, so `readonly restrict <&Self<T>>;` on 16 -- before any method is
// registered -- would have produced an interface with no methods, and every call
// through that member would have reported `has no member`.
// Soundness_SelfGenerics.SelfWithTheEnclosingParametersIsTheInterfaceItself.
//
// By position and by name, and only for a bare parameter name: `Self<B, A>` inside
// `IPair<A, B>` and `Self<int>` inside `Box<T>` are genuine instantiations and fall
// through to the general path. `&Self<T>` reaches here as the inner node, so the
// pointer_depth guard is about `Self<&T>`, which names something else again.
bool selfNamesEnclosingType(const TypeNode* node, const TypePtr& resolved) {
    if (!node || node->name != "Self" || !resolved) return false;

    // In a struct or class `Self` is a SelfType wrapping the struct; in an interface
    // it is the interface's own StructType (Analyzer_Decl.cpp:276 and :553).
    const std::vector<TypePtr>* params = nullptr;
    if (auto* self = resolved->as<SelfType>()) {
        if (auto s = std::dynamic_pointer_cast<StructType>(self->originalStruct))
            params = &s->generic_args;
    } else if (auto* st = resolved->as<StructType>()) {
        params = &st->generic_args;
    }
    if (!params || params->size() != node->generics.size() || params->empty()) return false;

    for (size_t i = 0; i < params->size(); ++i) {
        const TypeNode* written = node->generics[i].get();
        if (!written || !(*params)[i]) return false;
        if (!written->generics.empty() || written->pointer_depth != 0 ||
            written->is_array || written->is_nullable) return false;
        if (written->name != (*params)[i]->toString()) return false;
    }
    return true;
}

} // namespace

bool SemanticAnalyzer::constantFitsType(const ASTNode& node, const Type& target) {
    bool negative = false;
    if (!integerConstant(node, negative)) return false;

    const auto* prim = target.as<PrimitiveType>();
    if (!prim) return false;

    const auto info = scalarOf(*prim);
    if (!info) return false;
    if (info->kind == ScalarKind::Float) return true;
    if (info->kind != ScalarKind::Int || info->bits == 0) return false;

    if (negative) {
        if (!info->isSigned) return false;
        int64_t value = 0;
        if (readSignedConstant(node, value) != ConstantRead::Ok) return false;
        if (info->bits >= 64) return true;
        const int64_t minimum = -(int64_t{1} << (info->bits - 1));
        return value >= minimum;
    }

    uint64_t value = 0;
    if (readConstant(node, value) != ConstantRead::Ok) return false;
    if (info->isSigned) {
        const uint64_t maximum = info->bits >= 64
            ? static_cast<uint64_t>(INT64_MAX)
            : (uint64_t{1} << (info->bits - 1)) - 1;
        return value <= maximum;
    }
    const uint64_t maximum = info->bits >= 64
        ? UINT64_MAX
        : (uint64_t{1} << info->bits) - 1;
    return value <= maximum;
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

    // The short integer and float spellings. Widths and signs read off the one
    // scalar table (types/Layout.hpp), which already carries them so that
    // `u8` resolves to a width rather than to a second table somewhere else;
    // registering the names is what makes them writable in annotations. What
    // checking they get -- widening, narrowing, the negative-constant guard --
    // is asked of that table (scalarOf), not of the spelling, so no rule below
    // restates a width.
    currentScope->defineType("u8", std::make_shared<PrimitiveType>("u8"));
    currentScope->defineType("i8", std::make_shared<PrimitiveType>("i8"));
    currentScope->defineType("u16", std::make_shared<PrimitiveType>("u16"));
    currentScope->defineType("i16", std::make_shared<PrimitiveType>("i16"));
    currentScope->defineType("u32", std::make_shared<PrimitiveType>("u32"));
    currentScope->defineType("i32", std::make_shared<PrimitiveType>("i32"));
    currentScope->defineType("u64", std::make_shared<PrimitiveType>("u64"));
    currentScope->defineType("i64", std::make_shared<PrimitiveType>("i64"));
    currentScope->defineType("f32", std::make_shared<PrimitiveType>("f32"));
    currentScope->defineType("f64", std::make_shared<PrimitiveType>("f64"));
    currentScope->defineType("usize", std::make_shared<PrimitiveType>("usize"));
    currentScope->defineType("isize", std::make_shared<PrimitiveType>("isize"));
    currentScope->defineType("size_t", std::make_shared<PrimitiveType>("size_t"));
    
    // The two dynamic types. Builtins because the corpus uses them in files with no
    // imports at all -- `nullifier.fin:34` and `literal_struct.fin:4` write `any`,
    // `prototype_test.fin:40` writes `object` -- and two names rather than one alias
    // because the corpus distinguishes them: `any` is compile-time erasure
    // (stdlib/types.fin:97), `object` is a runtime box (prototype_test.fin:40). See
    // DynamicType.hpp for why that difference is not yet observable.
    //
    // `Any` is *not* registered here. `stdlib/types.fin:69` declares `pub type Any =
    // any;` behind `#[export]`, which makes it a library alias; handing it out with
    // the builtin would compile programs here that a real standard library rejects.
    // Soundness_DynamicTypes.AnUnknownTypeNameIsStillUndefined forbids it, along with
    // `Object` and `AnyType`, which are nobody's spelling.
    currentScope->defineType("any", std::make_shared<DynamicType>("any"));
    currentScope->defineType("object", std::make_shared<DynamicType>("object"));

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

    // `quote`, opaque. The layout projections (`compiler.layout.pointer_map_quote`)
    // and the provider slots return one, so the signature is nameable before
    // wave-4 steps 11-13 build quote values: resolving `<quote>` in a signature
    // is not evaluating it, and no quote value reaches any later pass yet. A
    // PrimitiveType for the `$` family's reason -- name equality is the behaviour
    // needed, and nothing treats "is a PrimitiveType" as "is a number".
    currentScope->defineType("quote", std::make_shared<PrimitiveType>("quote"));

    // `function`, opaque, for the `$` family's and `quote`'s reason: the
    // function_entry/function_exit payloads name it
    // (docs/compiler-api.md §3.2), so a handler must be able to spell it in
    // a signature. Resolving `<function>` is not carrying a value of it;
    // what a handler receives at a fire point is wave-4 step 17's firing,
    // which is not decided by registering the name.
    currentScope->defineType("function", std::make_shared<PrimitiveType>("function"));
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
std::shared_ptr<Type> SemanticAnalyzer::resolveTypeOrError(TypeNode* node) {
    auto t = resolveTypeFromAST(node);
    if (t) return t;
    // resolveTypeUnwrapped has already reported the cause. The sentinel's whole
    // job is to stop it being reported again, once per use of the declaration.
    //
    // Safe to substitute unconditionally because the grammar admits no untyped
    // declaration: `fun f(self)` is a syntax error ("expecting COLON"), and a
    // member without `<T>` likewise. A null here therefore always means a type
    // was written and failed to resolve, never that none was written. If the
    // grammar ever gains an inferred parameter, this must not paper over it --
    // `auto` is the spelling for that and it resolves to a real type.
    return errorType();
}

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

    // 2. Array Type. The extent is resolved into the type, not just validated.
    //
    // `[int, 4]` and `[int, 8]` were one type before this: the size was analysed
    // for its own diagnostics and the *value* went nowhere, so ArrayType had a
    // `fixed` flag and no number. That is why the layout pass refused every array
    // rather than only the dynamic ones -- any size it reported would have been a
    // guess, and a guessed size is a struct that is silently the wrong shape.
    //
    // Which makes a non-constant extent a refusal rather than a fallback. Storing
    // it as "fixed, size unknown" would be the same defect in a new spelling, and
    // silently demoting it to `[int]` would give the writer a type they did not
    // ask for. `new [T, n]` is the spelling for a run-time count of elements, and
    // visit(NewExpression) keeps it out of this path for exactly that reason.
    if (auto* arrNode = dynamic_cast<ArrayTypeNode*>(node)) {
        auto inner = resolveTypeFromAST(arrNode->element_type.get());
        std::optional<uint64_t> extent;

        if (arrNode->size) {
            arrNode->size->accept(*this);

            // Ensure it evaluates to an integer
            auto intType = currentScope->resolveType("int");
            bool integral = true;
            if (lastExprType) {
                if (!checkType(*arrNode->size, lastExprType, intType)) {
                    error(*arrNode->size, "Array size must be an integer");
                    integral = false;
                }
            }

            // Only when the type check agreed, so that a `[int, "x"]` gets the one
            // diagnostic about its type and not a second about its constness.
            if (integral) {
                uint64_t count = 0;
                switch (readExtent(*arrNode->size, count)) {
                    case ExtentRead::Ok:
                        extent = count;
                        break;
                    case ExtentRead::Negative:
                        error(*arrNode->size, "An array's size cannot be negative");
                        break;
                    case ExtentRead::TooLarge:
                        error(*arrNode->size, "An array's size is too large to represent");
                        break;
                    case ExtentRead::NotConstant:
                        error(*arrNode->size,
                              "An array's size must be a constant integer; `new [T, n]` "
                              "allocates a run-time number of elements");
                        break;
                }
            }
        }

        // After the size check, so that `[NoSuchType, wrongsize]` reports both.
        if (!inner) return nullptr;
        return std::make_shared<ArrayType>(inner, extent);
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
        // The declaring module, for a type inside a macro expansion (ADR 0023 step 4).
        // A library's macro spelling `Held::make($n)` names `Held` because the module that
        // wrote the macro imported it; the caller need not have, and under call-site-only
        // resolution never could without knowing the macro's body, which is ADR 0020's
        // objection to the C preprocessor.
        //
        // Second and not first, so a caller's own name still wins where both have one --
        // the macro asked for the type by that name, and shadowing it is the caller's
        // prerogative. Set on nothing a programmer wrote, so this line is unreachable for
        // every type outside an expansion.
        if (node->declaringScope) {
            type = node->declaringScope->resolveType(node->name);
        }
    }
    if (!type) {
        error(*node, "Undefined type '" + node->name + "'");
        return nullptr;
    }
    
    // 5. Generics
    if (!node->generics.empty() && !selfNamesEnclosingType(node, type)) {
        std::vector<std::shared_ptr<Type>> args;
        auto structDef = std::dynamic_pointer_cast<StructType>(type);
        bool argsResolved = true;
        
        for(size_t i = 0; i < node->generics.size(); ++i) {
            // `...` is elision: accepted as a generic argument and only there.
            // A bare `...` stays undefined, and what an elided argument constrains
            // is nothing -- dynamic targets drop their arguments unread already,
            // and anything else answers for itself downstream. This is what
            // `Any<...>` (stdlib/operators.fin:6) needs to resolve.
            if (node->generics[i]->name == "..." &&
                node->generics[i]->generics.empty()) {
                args.push_back(std::make_shared<DynamicType>("..."));
                continue;
            }
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
        } else if (auto* dyn = type->as<DynamicType>()) {
             // ADR 0038: A bound written Any<Printable> narrows to implementors of
             // Printable, so method calls through such a value resolve against the bound.
             // We record the generic arguments as bounds on the DynamicType.
             type = std::make_shared<DynamicType>(dyn->name, args);
        } else {
             type = std::make_shared<StructType>(node->name, args);
        }
    }
    
    // 6. The written width: `int{64}`.
    //
    // Resolved *into* the type, the way section 2 resolves an array's extent, and
    // for the same reason: until this the annotation was walked for its own
    // diagnostics and the value went nowhere, so `int{8}` and `int` were one
    // semantic type. That single missing number is three defects -- a narrowing
    // assignment with nothing narrower to refuse, a layout pass answering four
    // bytes for a one-byte field, and `expected 'uint'` shown to someone who wrote
    // `uint{8}` -- and PrimitiveType::bits is where it now lives.
    //
    // The annotation is still walked whatever it is written on, so a malformed
    // width is a diagnostic on `float{-8}` as much as on `int{-8}`; what depends on
    // the base type is only whether there is anywhere to *put* the number. A width
    // on a non-integer is dropped, because a width is a count of value bits, an
    // IEEE format is not built from one, and Fin has ruled on no floating-point
    // format but the two the table names -- so `float{128}` is `float`, which is
    // what tests/samples/type_annotations.fin:14 needs to keep resolving.
    //
    // Nothing reaches here from a pointer, an array, a function type or a
    // prototype: each of those returns above, so `(*int){32}` and `{int, float}{8}`
    // keep resolving with their annotation unread. That is the state those
    // spellings were already in and not a decision this section makes.
    if (type && !node->annotations.empty()) {
        for (auto& ann : node->annotations) {
            ann->accept(*this);
        }

        // Every annotation walked first, so `int{"a", "b"}` reports both of its own
        // mismatches, and then the count -- which is the array extent's ordering
        // read onto a list that may hold more than one thing.
        if (node->annotations.size() > 1) {
            error(*node, "A type takes one bit width");
            return type;
        }

        Expression& ann = *node->annotations[0];
        // Checked against `int` like any other expression, which is what reports
        // `expected 'int', got 'string'` for `int{"a"}`; the width diagnostic below
        // then says what it was written *as*. Two messages about different things,
        // exactly as `[int, "x"]` reports both.
        auto intType = currentScope->resolveType("int");
        // Keep an unrepresentable magnitude on the width-reading path. If it
        // became a normal type mismatch first, checkType would suppress the
        // width-specific diagnostic below.
        uint64_t writtenWidth = 0;
        if (readExtent(ann, writtenWidth) == ExtentRead::TooLarge) {
            error(ann, "A bit width is too large to represent");
            return type;
        }
        bool integral = true;
        if (lastExprType) {
            if (!checkType(ann, lastExprType, intType)) {
                error(ann, "A bit width must be an integer");
                integral = false;
            }
        }
        if (!integral) return type;

        uint64_t width = 0;
        switch (readExtent(ann, width)) {
            case ExtentRead::Ok:
                if (width == 0) {
                    // Separated from Negative because they are different mistakes and
                    // 0 is the one a reader can talk themselves into: a zero-bit
                    // integer holds no values, so there is nothing for it to be.
                    error(ann, "A bit width cannot be zero");
                    return type;
                }
                break;
            case ExtentRead::Negative:
                error(ann, "A bit width cannot be negative");
                return type;
            case ExtentRead::TooLarge:
                error(ann, "A bit width is too large to represent");
                return type;
            case ExtentRead::NotConstant:
                // Not a diagnostic, and this is the one case where a width differs
                // from an extent. tests/samples/type_annotations.fin:8 writes
                // `let z <int{8 * 8}> = 42;` in an `//@ ok` sample, and Fin has no
                // constant folder on purpose (utils/IntegerConstant.hpp: folding
                // arithmetic would answer an open language question by accident for
                // whichever subset happens to be foldable). So there is no width
                // here to store -- not a wrong one, none -- and the base name stands.
                // The backend still refuses the program, because a written annotation
                // that yielded no width is a width the program asked for and did not
                // get.
                return type;
        }

        // Only where the number has a meaning. `width` is a count of value bits, so
        // it needs an integer scalar to count the bits of; scalarByName is asked
        // rather than a list of names being restated, which is the same "one table"
        // rule ADR 0022 states for the widening itself.
        //
        // A fresh type rather than a mutation: `currentScope->resolveType("int")`
        // hands back the one registered `int`, and writing a width onto it would
        // make every unannotated `int` in the program 64 bits wide.
        if (auto* prim = type->as<PrimitiveType>()) {
            const auto info = scalarByName(prim->name);
            if (info && info->kind == ScalarKind::Int) {
                type = std::make_shared<PrimitiveType>(prim->name, static_cast<unsigned>(width));
            }
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
    // A quiet pre-pass reports nothing and, just as importantly, does not set
    // hasError: an exit code that says the program failed with no diagnostic printed
    // is the one outcome worse than a duplicate. See SemanticAnalyzer::QuietPass for
    // why silence is sound at the two sites that use it.
    if (quietDepth) return;
    if (activeAttr_.active)
        diag.reportError(node.loc, attributedMessage(msg), engineAttribution());
    else
        diag.reportError(node.loc, msg);
    hasError = true;
}

void SemanticAnalyzer::error(ASTNode& node, const std::string& msg,
                             const std::string& help) {
    if (quietDepth) return;
    if (activeAttr_.active)
        diag.reportError(node.loc, attributedMessage(msg), help, engineAttribution());
    else
        diag.reportError(node.loc, msg, help);
    hasError = true;
}

std::string SemanticAnalyzer::attributedMessage(const std::string& msg) const {
    if (!activeAttr_.active) return msg;
    return msg + " [injected by handler '" + activeAttr_.handler + "' for event '" +
           activeAttr_.event + "' at '" + activeAttr_.detail + "' (line " +
           std::to_string(activeAttr_.line) + ")]";
}

DiagnosticAttribution SemanticAnalyzer::engineAttribution() const {
    DiagnosticAttribution attr;
    if (activeAttr_.active) {
        attr.handler = activeAttr_.handler;
        attr.event = activeAttr_.event;
    }
    return attr;
}

void SemanticAnalyzer::warning(ASTNode& node, const std::string& msg) {
    if (quietDepth) return;
    diag.reportWarning(node.loc, attributedMessage(msg));
}

void SemanticAnalyzer::note(ASTNode& node, const std::string& msg) {
    if (quietDepth) return;
    diag.reportNote(node.loc, attributedMessage(msg));
}

// Wave-4 step 19: the check half of splice-then-check. Each chunk's
// statements are walked in a child of the anchor's recorded scope with that
// chunk's attribution active, so a diagnostic in generated code names the
// handler that wrote it and the event point that fired it. The child scope
// keeps what the check defines from leaking into a scope the walk already
// left; the saved stack, current scope, return-type context and type hint
// are restored afterwards, so the check is invisible to whatever follows.
void SemanticAnalyzer::checkInjectedChunks(const std::vector<events::InjectedChunk>& chunks) {
    if (chunks.empty()) return;
    const auto savedStack = scopeStack;
    const auto savedScope = currentScope;
    const auto savedRet = context.currentFuncReturnType;
    const ASTNode* savedHintFor = typeHintFor;
    const auto savedHint = typeHint;
    context.currentFuncReturnType = nullptr;
    typeHintFor = nullptr;
    typeHint = nullptr;
    injectedWalk_ = true;
    for (const auto& chunk : chunks) {
        const auto it = w5_scopes_.find(chunk.anchor);
        if (it != w5_scopes_.end() && !it->second.empty()) {
            scopeStack = it->second;
            currentScope = scopeStack.back();
        } else {
            // The anchor is recorded from this same tree, so a missing scope
            // is the compiler surprising itself: check against globals rather
            // than skipping the check.
            scopeStack.clear();
            scopeStack.push_back(globalScope);
            currentScope = globalScope;
        }
        enterScope();
        activeAttr_.handler = chunk.handler;
        activeAttr_.event = chunk.event;
        activeAttr_.detail = chunk.detail;
        activeAttr_.line = chunk.line;
        activeAttr_.active = true;
        for (auto* stmt : chunk.inserted) {
            if (stmt) stmt->accept(*this);
        }
        activeAttr_.active = false;
        exitScope();
    }
    injectedWalk_ = false;
    scopeStack = savedStack;
    currentScope = savedScope;
    context.currentFuncReturnType = savedRet;
    typeHintFor = savedHintFor;
    typeHint = savedHint;
}

bool SemanticAnalyzer::checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected) {
    if (!actual || !expected) return false;

    // Nothing to compare once the analyser has already failed to type one side. The
    // annotation that failed is the real diagnostic; a mismatch here would be a second
    // one, naming a type the program never wrote. isErrorType rather than a plain
    // as<ErrorType>() because `&NoSuchType` and `[NoSuchType]` reach here wrapped.
    if (isErrorType(actual) || isErrorType(expected)) return true;

    // A reference reads as its pointee where a value is expected (rvalues
    // deref, lvalues do not): `&T` is accepted for `T`, once, and never
    // through a nullable (narrow those first). Probed quietly so a miss still
    // reports the original mismatch rather than the pointee's -- and only one
    // level, so a `&&T` still needs an explicit `*`.
    if (auto* ptr = actual->as<PointerType>()) {
        if (ptr->pointee && !ptr->pointee->as<PointerType>() &&
            !ptr->pointee->as<NullableType>()) {
            QuietPass quiet(*this);
            if (checkType(node, ptr->pointee, expected)) return true;
        }
    }

    // A negative constant is not an unsigned value, whatever the widths say.
    //
    // Read before assignability and not after, because ADR 0022's widening makes
    // `int` -> `ulong` succeed and `constantFitsType` below only ever runs when
    // assignability has already failed. Without this, widening would smuggle in
    // `let x <ulong> = -1;` -- which is the one thing
    // Soundness_IntegerConstants.ANegativeConstantIsNotUnsigned exists to catch. That
    // test names this exact mistake ("a fix that admits `int` to `uint` wholesale
    // passes every test above and this one is the only thing that catches it") and
    // says the check must read the AST, because `-1` is a UnaryOp over a Literal and
    // so a syntactic question with an exact answer.
    //
    // The widening ruling did not settle this one. tests/samples/stdlib/stdio.fin:109
    // writes `fun read(nbytes: ulong = -1)` and :110 tests `nbytes == -1`, the C idiom
    // for "the maximum", so a normative sample does ask for wraparound -- and that is
    // the open ruling the Soundness test names, with the two outcomes it lists: invert
    // the test and drop the `!negative` in constantFitsType, or stdio.fin gains a
    // ratified edit. Widening must not decide it as a side effect, so :110 stays
    // refused exactly as it was before ADR 0022, and what widening clears in that file
    // is :130 and :135 -- `int` to `ulong` with no constant in sight.
    {
        bool negative = false;
        if (integerConstant(node, negative) && negative) {
            if (const auto* prim = expected->as<PrimitiveType>()) {
                if (isUnsignedIntegerName(prim->name)) {
                    error(node, fmt::format("Type mismatch: expected '{}', got '{}'",
                                            expected->toString(), actual->toString()));
                    return false;
                }
            }
        }
    }

    // Widening is normally enough to make an integer assignment legal, but a
    // constant still has to fit the target. Check this successful path too;
    // the narrowing path below uses the same rule without double-reporting.
    bool constantNegative = false;
    const bool isConstant = integerConstant(node, constantNegative);
    const bool assignable = actual->isAssignableTo(*expected);
    const auto* expectedPrim = expected->as<PrimitiveType>();
    const auto expectedInfo = expectedPrim ? scalarOf(*expectedPrim)
                                           : std::optional<ScalarInfo>{};
    const bool numericTarget = expectedInfo &&
        (expectedInfo->kind == ScalarKind::Int || expectedInfo->kind == ScalarKind::Float);
    if (assignable && isConstant && numericTarget) {
        bool fits = constantFitsType(node, *expected);
        if (!fits) {
            const auto* prim = expected->as<PrimitiveType>();
            const auto info = prim ? scalarOf(*prim) : std::optional<ScalarInfo>{};
            bool negative = false;
            integerConstant(node, negative);
            uint64_t magnitude = 0;
            const auto read = negative
                ? ConstantRead::NotConstant
                : readConstant(node, magnitude);
            if (info && info->kind == ScalarKind::Int && !negative &&
                !info->isSigned && info->bits >= 64 && read == ConstantRead::TooLarge) {
                error(node, "An integer constant is too large to represent");
            } else {
                error(node, fmt::format("Type mismatch: expected '{}', got '{}'",
                                        expected->toString(), actual->toString()));
            }
            return false;
        }
    }

    if (!assignable) {
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

void SemanticAnalyzer::visitParameterDefaults(const std::vector<std::unique_ptr<Parameter>>& params) {
    for (auto& param : params) {
        if (!param->default_value) continue;
        param->default_value->accept(*this);

        // The declared type, re-resolved rather than passed in. Every one of the nine
        // callers resolves the same TypeNode a few lines above this call, so the
        // answer is already known there -- but not in a form this helper can be
        // handed: three of them drop a receiver or an enum's first parameter from the
        // vector they build, so `paramTypes[i]` and `params[i]` are not the same
        // parameter. Aligning them would mean a second vector at nine sites, which is
        // the "N copies of one loop" shape this helper exists to remove.
        //
        // Quiet, because the caller has already reported anything that does not
        // resolve: resolveTypeOrError runs first at all nine sites. Without the
        // QuietPass, `fun f(p: NoSuchType = 1)` would report its undefined type
        // twice, and Soundness_ErrorRecovery is what would catch it.
        std::shared_ptr<Type> declared;
        {
            QuietPass quiet(*this);
            declared = resolveTypeFromAST(param->type.get());
        }
        // No guard on either side of the comparison, deliberately, and each absence was
        // mutation-tested: deleting an `isErrorType(declared)` guard and deleting a
        // `!lastExprType` guard both left every one of the 1396 tests green, because
        // checkType already answers both -- it returns false silently on a null and true
        // on the error sentinel, which is where that suppression belongs and where every
        // other caller relies on it. A guard here that no test can distinguish from its
        // absence is a claim about behaviour that is not true.
        //
        // checkInitializer and not checkType, which was known before this was written
        // rather than discovered after: a mutation over the walk half applied the naive
        // version and killed ANullDefaultIsStillAccepted, because stdlib/error.fin:11
        // writes `err_code: int = null` and a plain checkType has no null exemption. A
        // default is an initialiser -- `= null` means "absent" here exactly as it does
        // on a field or a `let` (see checkInitializer's own comment).
        checkInitializer(*param->default_value, lastExprType, declared);
    }
}

// Unreachable, and left in place because the Visitor interface requires it: no
// parameter loop dispatches to it, they all walk `param->type` directly. Logic
// added here will not run -- see visitParameterDefaults.
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


void SemanticAnalyzer::hoistTopLevelSignatures(Program& node) {
    QuietPass quiet(*this);
    hoistedTypes_.clear();

    // Pass 1: Declare named types at file scope so types can refer to each other
    for (auto& stmt : node.statements) {
        if (auto* s = dynamic_cast<StructDeclaration*>(stmt.get())) {
            if (!currentScope->types.count(s->name)) {
                auto structType = std::make_shared<StructType>(s->name);
                currentScope->defineType(s->name, structType);
                hoistedTypes_[s] = structType;
                debugLog(fg(fmt::color::gray), "      [Hoist] Registered struct '{}' at file scope\n", s->name);
            }
        } else if (auto* cls = dynamic_cast<ClassDeclaration*>(stmt.get())) {
            if (!currentScope->types.count(cls->name)) {
                auto structType = std::make_shared<StructType>(cls->name);
                currentScope->defineType(cls->name, structType);
                hoistedTypes_[cls] = structType;
                debugLog(fg(fmt::color::gray), "      [Hoist] Registered class '{}' at file scope\n", cls->name);
            }
        } else if (auto* iface = dynamic_cast<InterfaceDeclaration*>(stmt.get())) {
            if (!currentScope->types.count(iface->name)) {
                auto ifaceType = std::make_shared<StructType>(iface->name);
                ifaceType->is_interface = true;
                currentScope->defineType(iface->name, ifaceType);
                hoistedTypes_[iface] = ifaceType;
                debugLog(fg(fmt::color::gray), "      [Hoist] Registered interface '{}' at file scope\n", iface->name);
            }
        } else if (auto* en = dynamic_cast<EnumDeclaration*>(stmt.get())) {
            if (!currentScope->types.count(en->name)) {
                auto enumType = std::make_shared<StructType>(en->name);
                enumType->is_enum = true;
                currentScope->defineType(en->name, enumType);
                hoistedTypes_[en] = enumType;
                debugLog(fg(fmt::color::gray), "      [Hoist] Registered enum '{}' at file scope\n", en->name);
            }
        } else if (auto* td = dynamic_cast<TypeDefinition*>(stmt.get())) {
            if (!td->is_extern_wildcard && !td->is_extern_alias && !td->is_symbol_resolution &&
                td->generic_params.empty() && td->aliased_type && !currentScope->types.count(td->name)) {
                auto type = resolveTypeFromAST(td->aliased_type.get());
                if (type && !isErrorType(type)) {
                    currentScope->defineType(td->name, type);
                    debugLog(fg(fmt::color::gray), "      [Hoist] Registered type alias '{}' at file scope\n", td->name);
                }
            }
        }
    }

    // Pass 2: Register members, fields, methods, and enumerators on the types
    for (auto& stmt : node.statements) {
        if (auto* s = dynamic_cast<StructDeclaration*>(stmt.get())) {
            auto it = hoistedTypes_.find(s);
            if (it == hoistedTypes_.end()) continue;
            auto structType = it->second;

            enterScope();
            declareGenericParams(s->generic_params, &structType->generic_args);
            currentScope->defineType("Self", std::make_shared<SelfType>(structType));

            for (auto& parentNode : s->parents) {
                auto parentType = resolveTypeFromAST(parentNode.get());
                if (parentType) {
                    if (auto p = std::dynamic_pointer_cast<StructType>(parentType)) {
                        bool found = false;
                        for (auto& existingP : structType->parents) {
                            if (existingP->equals(*p)) { found = true; break; }
                        }
                        if (!found) structType->parents.push_back(p);
                    }
                }
            }

            for (auto& member : s->members) {
                auto memberType = resolveTypeFromAST(member->type.get());
                if (memberType && !isErrorType(memberType)) {
                    structType->defineField(member->name, memberType, member->is_public, member->is_readonly);
                }
            }

            for (auto& method : s->methods) {
                if (auto sig = buildMethodSignature(*method)) {
                    structType->defineMethod(method->name, sig, method->is_public);
                    recordMethodGenerics(s->name, method->name, method->generic_params);
                }
            }
            for (auto& op : s->operators) {
                if (auto sig = buildOperatorSignature(*op, structType))
                    structType->defineOperator((int)op->op, sig);
            }
            for (auto& ctor : s->constructors) {
                enterScope();
                std::vector<std::shared_ptr<Type>> paramTypes;
                std::vector<bool> paramDefaults;
                for (auto& param : ctor->params) {
                    auto pt = resolveTypeFromAST(param->type.get());
                    if (pt && !isErrorType(pt)) paramTypes.push_back(pt);
                    paramDefaults.push_back(param->default_value != nullptr);
                }
                auto ctorType = std::make_shared<FunctionType>(paramTypes, structType, false, paramDefaults);
                structType->addConstructor(ctorType);
                exitScope();
            }

            exitScope();
        } else if (auto* cls = dynamic_cast<ClassDeclaration*>(stmt.get())) {
            auto it = hoistedTypes_.find(cls);
            if (it == hoistedTypes_.end()) continue;
            auto structType = it->second;

            enterScope();
            declareGenericParams(cls->generic_params, &structType->generic_args);
            currentScope->defineType("Self", std::make_shared<SelfType>(structType));

            for (auto& parentNode : cls->parents) {
                auto parentType = resolveTypeFromAST(parentNode.get());
                if (parentType) {
                    if (auto p = std::dynamic_pointer_cast<StructType>(parentType)) {
                        bool found = false;
                        for (auto& existingP : structType->parents) {
                            if (existingP->equals(*p)) { found = true; break; }
                        }
                        if (!found) structType->parents.push_back(p);
                    }
                }
            }

            for (auto& member : cls->members) {
                auto memberType = resolveTypeFromAST(member->type.get());
                if (memberType && !isErrorType(memberType)) {
                    structType->defineField(member->name, memberType, member->is_public, member->is_readonly);
                }
            }

            for (auto& method : cls->methods) {
                if (auto sig = buildMethodSignature(*method)) {
                    structType->defineMethod(method->name, sig, method->is_public);
                    recordMethodGenerics(cls->name, method->name, method->generic_params);
                }
            }
            for (auto& op : cls->operators) {
                if (auto sig = buildOperatorSignature(*op, structType))
                    structType->defineOperator((int)op->op, sig);
            }
            for (auto& ctor : cls->constructors) {
                enterScope();
                std::vector<std::shared_ptr<Type>> paramTypes;
                std::vector<bool> paramDefaults;
                for (auto& param : ctor->params) {
                    auto pt = resolveTypeFromAST(param->type.get());
                    if (pt && !isErrorType(pt)) paramTypes.push_back(pt);
                    paramDefaults.push_back(param->default_value != nullptr);
                }
                auto ctorType = std::make_shared<FunctionType>(paramTypes, structType, false, paramDefaults);
                structType->addConstructor(ctorType);
                exitScope();
            }

            exitScope();
        } else if (auto* iface = dynamic_cast<InterfaceDeclaration*>(stmt.get())) {
            auto it = hoistedTypes_.find(iface);
            if (it == hoistedTypes_.end()) continue;
            auto ifaceType = it->second;

            enterScope();
            declareGenericParams(iface->generic_params, &ifaceType->generic_args);
            currentScope->defineType("Self", ifaceType);

            for (auto& member : iface->members) {
                auto memberType = resolveTypeFromAST(member->type.get());
                if (memberType && !isErrorType(memberType)) {
                    ifaceType->defineField(member->name, memberType, member->is_public, member->is_readonly);
                }
            }
            for (auto& method : iface->methods) {
                if (auto sig = buildMethodSignature(*method)) {
                    ifaceType->defineMethod(method->name, sig, method->is_public);
                    recordMethodGenerics(iface->name, method->name, method->generic_params);
                }
            }
            for (auto& op : iface->operators) {
                if (auto sig = buildOperatorSignature(*op, ifaceType))
                    ifaceType->defineOperator((int)op->op, sig);
            }
            for (auto& ctor : iface->constructors) {
                enterScope();
                std::vector<std::shared_ptr<Type>> paramTypes;
                std::vector<bool> paramDefaults;
                for (auto& param : ctor->params) {
                    auto pt = resolveTypeFromAST(param->type.get());
                    if (pt && !isErrorType(pt)) paramTypes.push_back(pt);
                    paramDefaults.push_back(param->default_value != nullptr);
                }
                auto ctorType = std::make_shared<FunctionType>(paramTypes, ifaceType, false, paramDefaults);
                ifaceType->addConstructor(ctorType);
                exitScope();
            }

            exitScope();
        } else if (auto* en = dynamic_cast<EnumDeclaration*>(stmt.get())) {
            auto it = hoistedTypes_.find(en);
            if (it == hoistedTypes_.end()) continue;
            auto enumType = it->second;

            std::vector<std::string> enumeratorNames;
            enumeratorNames.reserve(en->values.size());

            enterScope();
            declareGenericParams(en->generic_params, &enumType->generic_args);
            currentScope->defineType("Self", enumType);

            for (size_t i = 0; i < en->values.size(); ++i) {
                auto& val = en->values[i];
                std::vector<std::shared_ptr<Type>> payload;
                if (i < en->member_payloads.size() && en->member_payloads[i].name == val.first) {
                    for (auto& t : en->member_payloads[i].types) {
                        auto pt = resolveTypeFromAST(t.get());
                        if (pt && !isErrorType(pt)) payload.push_back(pt);
                    }
                }
                enumType->defineEnumerator(val.first, std::make_shared<FunctionType>(payload, enumType));
                enumeratorNames.push_back(val.first);
            }
            exitScope();

            for (const auto& name : enumeratorNames) {
                if (!currentScope->symbols.count(name)) {
                    currentScope->define({name, enumType->getEnumeratorValueType(name), false, true});
                }
            }
        } else if (auto* ib = dynamic_cast<ImplementsBlock*>(stmt.get())) {
            auto targetType = currentScope->resolveType(ib->target_type);
            auto structType = std::dynamic_pointer_cast<StructType>(targetType);
            if (!structType) continue;

            enterScope();
            currentScope->defineType("Self", structType);
            if (!ib->target_generics.empty()) {
                const size_t n = std::min(ib->target_generics.size(), structType->generic_args.size());
                for (size_t i = 0; i < n; ++i) {
                    const std::string& written = ib->target_generics[i]->name;
                    if (currentScope->resolveType(written)) continue;
                    currentScope->defineType(written, structType->generic_args[i]);
                }
            }
            for (auto& method : ib->methods) {
                if (auto sig = buildMethodSignature(*method, structType)) {
                    structType->defineMethod(method->name, sig, method->is_public);
                    recordMethodGenerics(structType->name, method->name, method->generic_params);
                }
            }
            for (auto& op : ib->operators) {
                if (auto sig = buildOperatorSignature(*op, structType))
                    structType->defineOperator((int)op->op, sig);
            }
            exitScope();
        }
    }

    // Pass 3: Register explicitly-typed global variables
    for (auto& stmt : node.statements) {
        if (auto* var = dynamic_cast<VariableDeclaration*>(stmt.get())) {
            if (var->type && !currentScope->symbols.count(var->name)) {
                auto type = resolveTypeFromAST(var->type.get());
                if (type && !isErrorType(type) && type->toString() != "auto") {
                    currentScope->define({var->name, type, var->is_mutable, var->initializer != nullptr});
                    debugLog(fg(fmt::color::gray), "      [Hoist] Registered global '{}' at file scope\n", var->name);
                }
            }
        }
    }

    // Pass 4: Register function and special signatures
    for (auto& stmt : node.statements) {
        std::string name;
        const std::vector<std::unique_ptr<Parameter>>* params = nullptr;
        const std::vector<std::unique_ptr<GenericParam>>* generics = nullptr;
        TypeNode* declaredReturn = nullptr;

        if (auto* fn = dynamic_cast<FunctionDeclaration*>(stmt.get())) {
            name = fn->name;
            params = &fn->params;
            generics = &fn->generic_params;
            declaredReturn = fn->return_type.get();
        } else if (auto* sp = dynamic_cast<SpecialDeclaration*>(stmt.get())) {
            // A `@special` carries no generic parameters -- the grammar gives it none.
            name = sp->name;
            params = &sp->params;
            declaredReturn = sp->return_type.get();
        } else {
            continue;
        }

        // A scope of its own, for the generics the signature may mention, discarded
        // once the signature is built. The GenericTypes it declared survive inside the
        // FunctionType, which is what visit(FunctionDeclaration&) does too.
        enterScope();
        if (generics) declareGenericParams(*generics);

        bool resolved = true;
        std::vector<std::shared_ptr<Type>> paramTypes;
        // The hoisted signature carries defaults for the same reason it carries
        // parameter types: it is what a call *above* the declaration is checked
        // against. Without this, `f(1)` before `fun f(a: int, b: int = 2)` reported an
        // arity error while the identical call below it did not -- the defaultedness
        // would have depended on which side of the declaration the call sat on.
        std::vector<bool> paramDefaults;
        for (auto& param : *params) {
            auto type = resolveTypeFromAST(param->type.get());
            if (!type || isErrorType(type)) { resolved = false; break; }
            paramTypes.push_back(type);
            paramDefaults.push_back(param->default_value != nullptr);
        }

        std::shared_ptr<Type> retType;
        if (resolved) {
            // Null return_type means none was written, which is `void` -- the same
            // reading as step 5 of visit(FunctionDeclaration&).
            retType = declaredReturn ? resolveTypeFromAST(declaredReturn)
                                     : currentScope->resolveType("void");
            if (!retType || isErrorType(retType)) resolved = false;
        }
        exitScope();

        if (!resolved) continue;

        // Not `define` unconditionally: a name already bound at file scope was bound by
        // something the pre-pass does not model -- an import, or a `const` above -- and
        // overwriting it here would let a function declared at the bottom of the file
        // silently take a name the top of the file already gave to something else. The
        // in-order walk still overwrites when its turn comes, which is the behaviour
        // that was already there.
        if (currentScope->symbols.count(name)) continue;

        currentScope->define({name, std::make_shared<FunctionType>(paramTypes, retType,
                                                                   false, paramDefaults),
                              false, true});
        if (generics) recordFunctionGenerics(name, *generics);
        debugLog(fg(fmt::color::gray), "      [Hoist] Registered '{}' at file scope\n", name);
    }
}

void SemanticAnalyzer::setExternalGlobalScope(const std::shared_ptr<Scope>& scope) {
    if (!scope || scope.get() == globalScope.get()) return;
    // Keep builtins and this module's declarations local, while resolving names
    // through the loader-owned ambient scope.
    globalScope->parent = scope.get();
}

void SemanticAnalyzer::visit(Program& node) {
    refuseMisplacedGlobals(node);
    auto prevHoisted = std::move(hoistedTypes_);
    // Wave-4 step 15, Collect then Arm (docs/compiler-api.md §3.4). Collection
    // runs before anything is analysed, so the subscription table exists before
    // any body that could fire it; arming consumes every top-level enable next,
    // so the armed set is complete before bodies are walked either.
    auto report = [this](ASTNode& at, const std::string& msg) { error(at, msg); };
    events::collectProgramHandlers(node, module_path_, event_registry_, report);
    events::armProgramHandlers(node, event_registry_, report);    // Wave-4 step 17: W6's payload match, before anything is analysed (§3.8).
    // W6 events only; W5's check owns W5's events.
    events::checkW6HandlerPayloads(node, event_registry_, report);
    // Wave-4 step 17 (W5 floor): the same pre-pass for W5's two events. The
    // refused set travels to the firing pass so one mismatch is diagnosed
    // once, never as a check-then-fire cascade.
    w5_refused_ = events::checkW5HandlerPayloads(node, event_registry_, report);
    // Wave-4 step 17 (W7): the same pre-pass for variable_scope_exit. W7's
    // event only; W5's and W6's checks own theirs, so one handler is
    // diagnosed once no matter how many events it subscribes.
    w7_refused_ = events::checkW7HandlerPayloads(node, event_registry_, report);
    // Wave-4 step 20 (W10): the same pre-pass for loop_back_edge. W10's
    // event only; older checks own theirs, so one handler is diagnosed
    // once no matter how many events it subscribes.
    w10_refused_ = events::checkW10HandlerPayloads(node, event_registry_, report);
    hoistTopLevelSignatures(node);
    // The program being walked, for the inline struct_layout_finalised fire.
    w5_program_ = &node;
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
    w5_program_ = nullptr;
    // Deferred variable_declared firing: splicing mid-walk would mutate the
    // vector being walked, so the accumulated points fire here, after every
    // body is analysed. struct_layout_finalised already fired inline.
    // Step 19: warnings and notes have their own reporters so a handler's
    // `compiler.diag.warning`/`note` never fails the build; chunks carry the
    // spliced batches into the attributed check below.
    auto warnReport = [this](ASTNode& at, const std::string& msg) { warning(at, msg); };
    auto noteReport = [this](ASTNode& at, const std::string& msg) { note(at, msg); };
    std::vector<events::InjectedChunk> w5chunks;
    for (auto& fired : events::fireW5Events(node, event_registry_, w5_points_, w5_refused_,
                                            report, warnReport, noteReport, &w5chunks))
        w5_fired_.push_back(std::move(fired));
    checkInjectedChunks(w5chunks);
    // Wave-4 step 17 (W7): variable_scope_exit fires deferred, in the same
    // shape as variable_declared: the walk accumulated one point per
    // variable per exit path, and splicing here never mutates the vector
    // being walked. After W5's firing: anchors are heap pointers, so earlier
    // inserts shift nothing they hold. Injected code does not fire events
    // (§3.3): the check walk above ran with injectedWalk_ set, during which
    // every hook below stayed silent.
    for (auto& fired : events::fireW7Events(node, event_registry_, moved_.points(),
                                            w7_refused_, report))
        w7_fired_.push_back(std::move(fired));
    // Wave-4 step 20 (W10): loop_back_edge fires deferred, after W7. The walk
    // accumulated one latch point per loop statement; prepending to body
    // blocks never mutates the vector being walked (the walk is over), and
    // each point anchors its own block, so firing order shifts no other
    // point's site. No codegen support is needed: the spliced quote is
    // ordinary Fin lowered by the existing loop lowering. The event fires per
    // static edge, not per iteration, so an infinite-loop program terminates
    // analysis here exactly as a bounded one does.
    for (auto& fired : events::fireW10Events(node, event_registry_, w10_points_,
                                            w10_refused_, report))
        w10_fired_.push_back(std::move(fired));
    // Wave-5 slice 0: the protocol claim registry's deferred refusal. A lone
    // well-formed claimant per slot is recognized and recorded, but
    // replacement is not lowered yet -- so it fails here, naming the claim
    // site, rather than compiling as if the library took over. After all
    // firing so every claimant in the module is collected; slots with zero
    // claimants report nothing and the default lowering stands.
    reportSingleProtocolClaims();
    dropConsumedImports(node);
    hoistedTypes_ = std::move(prevHoisted);
}

namespace {

// Reports every `#[global]` that the parser did not stamp as std-scoped.
//
// A walk over *attributes* rather than over declaration shapes, deliberately.
// `#[global]` is legal on any declaration the grammar accepts an attribute on, and
// an enforcement written as "check it on a function, and on a special, and on a
// variable, and ..." is wrong the day a shape is added and nobody remembers this
// list -- the failure mode is silence, which is the one this rule exists to
// prevent. StructuralWalk knows every node's children (ADR 0004), so the check
// cannot miss a declaration it was never told about.
//
// `unregisteredNode` is left at its throwing default for the same reason the
// parser's marker leaves it: a node type missing from FIN_NODE_LIST would skip a
// whole subtree, and a skipped subtree here means a misplaced `#[global]` accepted
// in silence.
class MisplacedGlobalFinder : public StructuralWalk {
public:
    std::vector<Attribute*> found;

protected:
    bool enter(ASTNode& node) override {
        if (node.kind() == NodeKind::Attribute) {
            auto& attr = static_cast<Attribute&>(node);
            if (attr.name == kGlobalAttribute && attr.is_flag && !attr.std_scoped) {
                found.push_back(&attr);
            }
        }
        return true;
    }
};

} // namespace

// Runs before anything else in the program, so the diagnostic is not buried under
// the cascade a name that failed to resolve would produce.
//
// Every misplaced one is reported, not just the first: a file with three of them
// has three things to fix, and reporting one at a time makes that three edit-build
// cycles. Same reasoning as codegen collecting every refusal.
void SemanticAnalyzer::refuseMisplacedGlobals(Program& node) {
    MisplacedGlobalFinder finder;
    finder.walkAll(node.statements);
    for (Attribute* attr : finder.found) {
        error(*attr,
              "#[global] is usable only inside `namespace std` (ADR 0021). A "
              "declaration marked #[global] is visible to every file in the "
              "compilation with no import, and a library that could mint such names "
              "would collide with another library in a third file that imported "
              "neither");
    }
}

// The last thing the front end does to the tree, and the reason it is a separate
// sweep rather than part of the walk above: erasing from `node.statements` while
// iterating it invalidates the iterator, and `visit(ImportModule&)` is reached
// through `accept` and has no handle on the vector holding it anyway.
//
// Only the root Program matters -- a module's own AST lives in the loader's
// `astStorage` and never reaches the backend -- but this runs for both, because a
// consumed import is spent in a module for exactly the same reason.
void SemanticAnalyzer::dropConsumedImports(Program& node) {
    node.statements.erase(
        std::remove_if(node.statements.begin(), node.statements.end(),
                       [](const std::unique_ptr<Statement>& stmt) {
                           auto* imp = dynamic_cast<ImportModule*>(stmt.get());
                           return imp && imp->consumed;
                       }),
        node.statements.end());
}

void SemanticAnalyzer::visit(TypeNode& node) { resolveTypeFromAST(&node); }
void SemanticAnalyzer::visit(FunctionTypeNode& node) { resolveTypeFromAST(&node); }

void SemanticAnalyzer::recordFunctionGenerics(
    const std::string& name, const std::vector<std::unique_ptr<GenericParam>>& params) {
    auto& slot = functionGenericOrder_[name];
    slot.clear();
    for (auto& p : params) slot.push_back(p->name);
}

void SemanticAnalyzer::recordMethodGenerics(
    const std::string& structName, const std::string& methodName,
    const std::vector<std::unique_ptr<GenericParam>>& params) {
    auto& slot = methodGenericOrder_[structName + "::" + methodName];
    slot.clear();
    for (auto& p : params) slot.push_back(p->name);
}

const std::vector<std::string>* SemanticAnalyzer::lookupFunctionGenerics(
    const std::string& name) const {
    auto it = functionGenericOrder_.find(name);
    return it != functionGenericOrder_.end() ? &it->second : nullptr;
}

const std::vector<std::string>* SemanticAnalyzer::lookupMethodGenerics(
    const std::shared_ptr<StructType>& structType, const std::string& methodName) const {
    for (auto cur = structType; cur;) {
        auto it = methodGenericOrder_.find(cur->name + "::" + methodName);
        if (it != methodGenericOrder_.end()) return &it->second;
        std::shared_ptr<StructType> next;
        for (auto& p : cur->parents) {
            if (auto ps = std::dynamic_pointer_cast<StructType>(p)) { next = ps; break; }
        }
        cur = next;
    }
    return nullptr;
}

}