#include "../SemanticAnalyzer.hpp"
#include "../../types/TypeImpl.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

// Signature now accepts std::shared_ptr<Scope>
std::shared_ptr<StructType> getStructType(std::shared_ptr<Type> type, std::shared_ptr<Scope> scope) {
    if (!type) return nullptr;

    // 1. Unwrap Pointers (recursive)
    if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
        return getStructType(ptr->pointee, scope);
    }

    // 2. Unwrap SelfType
    if (auto* self = dynamic_cast<const SelfType*>(type.get())) {
        return getStructType(self->originalStruct, scope);
    }

    // 3. Check if it's a StructType
    if (auto* st = dynamic_cast<StructType*>(type.get())) {
        return std::static_pointer_cast<StructType>(type);
    }

    // 4. Check GenericType (Constraint)
    if (auto* gt = dynamic_cast<GenericType*>(type.get())) {
        // If T : Interface, treat T as Interface
        if (gt->constraint) {
            return getStructType(gt->constraint, scope);
        }
    }

    return nullptr;
}

// Does this type have members even though it is not a struct?
//
// Two do, both because the corpus reads a member off them and neither because anything
// declares one: an array has `.length` (arrays.fin:12,17,18 on a `&[T]`, loops.fin:14
// on a `[int, 5]`, const.fin:67 through an rptr) and so does a string
// (stdlib/stdio.fin:160, `path.length == 10`). Before this every one of those five
// sites reported `Type '<the array>' is not a struct`, which is what visit(MemberAccess&)
// says when getStructType() comes back empty.
//
// The question is separate from "is *this* member one of them" on purpose, and the call
// site needs both: a type with no member set at all keeps the old `is not a struct`
// message, because for an `int` that message is the correct one -- there is no member
// set for a member to be missing from -- while a type that has one and lacks the member
// asked for gets `has no member`, so a typo stays a diagnostic instead of being handed
// back a type. Soundness_BuiltinMembers holds both halves.
bool typeHasBuiltinMembers(const TypePtr& type) {
    if (!type) return false;

    // Through pointers, recursively, exactly as getStructType does it above. Not an
    // extra: arrays.fin passes its arrays by reference so that they are not copied (its
    // own comment on :10 says so), which makes all three of its sites a member access
    // on a `&[T]` rather than on a `[T]`.
    if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
        return typeHasBuiltinMembers(ptr->pointee);
    }

    // Fixed and dynamic alike. ArrayType carries is_fixed_size and nothing here reads
    // it, because a count is a count either way; a fixed array's length could be folded
    // to a literal later without changing its type.
    if (dynamic_cast<const ArrayType*>(type.get())) return true;

    // A string is a PrimitiveType, not an array of char, so it needs saying separately.
    // `[char]` is a different type and gets its length from the line above -- which is
    // why stdlib/stdio.fin reads a length off both spellings and needs both rules.
    if (auto* prim = dynamic_cast<const PrimitiveType*>(type.get())) {
        return prim->name == "string";
    }

    // Deliberately not through a NullableType. `a.length` on a `[int]?` would be reading
    // a count out of a possibly-absent array, and the rule everywhere else is that a
    // nullable is narrowed before it is used; adding the hop here would exempt `.length`
    // from that rule without anyone deciding to. No sample writes it. Whoever rules on
    // the nullability edges owns this paragraph.
    //
    // Prototypes are absent for a different reason: stdlib/prototypes.fin:11,15 do read
    // members off one (`prtp.0`, `prtp.1`), but those return an array of the keys and an
    // array of the values, not a count -- a separate unit with a separate spec, and
    // wiring it in here as a `.length` would be inventing a member the corpus never
    // writes.
    return false;
}

void SemanticAnalyzer::visit(PrototypeLiteral& node) {
    std::shared_ptr<Type> keyType = nullptr;
    std::shared_ptr<Type> valueType = nullptr;

    // A heterogeneous literal widens to `object`, not to `any`. The two are not
    // interchangeable and prototype_test.fin says which is which: :14 writes
    // `<{object, string}>` over a literal whose keys are an int and a string, and :40
    // explains the choice -- "object type is an expensive type but can fit any datatype
    // in it at the cost of memory and speed". A cost paid at run time is a boxed value.
    // `any` is the other half of the pair: stdlib/types.fin:97 calls it "any type that
    // is visible in compile time", i.e. erasure of a type the compiler still knows.
    //
    // Here the compiler does *not* still know it. Two keys of different types have to
    // coexist in one container at run time, and only the boxed representation can hold
    // them. Inferring `any` claimed the opposite and had a second consequence: the
    // inferred type printed as `<{any, any}>` in every mismatch about a mixed literal,
    // which is a claim about representation the diagnostic had no business making.
    // An element that did not type becomes the sentinel, not `any`. Substituting `any`
    // was a claim -- `let a <{int, int}> = { nosuchvar : 1 };` reported the undefined
    // name and then `expected '<{int, int}>', got '<{any, int}>'`, naming a boxed key
    // the program never asked for. The sentinel is the one type that absorbs the second
    // comparison instead of losing it (isErrorType unwraps a prototype for this).
    //
    // And once a side is the sentinel it stays the sentinel: the widening below must not
    // overwrite it, or `{ nosuchvar : 1, 5 : 2 }` would see `<error>` and `int` disagree,
    // widen to `object`, and put the cascade back with a different type in it.
    for (auto& pair : node.elements) {
        pair.first->accept(*this);
        auto kType = lastExprType ? lastExprType : errorType();
        if (!keyType) keyType = kType;
        else if (isErrorType(kType) || isErrorType(keyType)) keyType = errorType();
        else if (!kType->equals(*keyType)) keyType = currentScope->resolveType("object");

        pair.second->accept(*this);
        auto vType = lastExprType ? lastExprType : errorType();
        if (!valueType) valueType = vType;
        else if (isErrorType(vType) || isErrorType(valueType)) valueType = errorType();
        else if (!vType->equals(*valueType)) valueType = currentScope->resolveType("object");
    }

    // Unreachable while `{}` is a syntax error (`unexpected RBRACE`), and a total guard
    // rather than an assertion because the parser is the only thing keeping it that way.
    // It is the sentinel and not a fabricated `PrimitiveType("any")`, which is what
    // stood here: `any` is a registered DynamicType now, so a hand-built primitive of
    // the same spelling would print as `any` and behave as none of it.
    if (!keyType) keyType = errorType();
    if (!valueType) valueType = errorType();

    lastExprType = std::make_shared<PrototypeType>(keyType, valueType);
}

void SemanticAnalyzer::visit(Literal& node) {
    switch(node.kind) {
        case ASTTokenKind::INTEGER: lastExprType = currentScope->resolveType("int"); break;
        case ASTTokenKind::FLOAT:   lastExprType = currentScope->resolveType("float"); break;
        case ASTTokenKind::STRING_LITERAL:  lastExprType = currentScope->resolveType("string"); break;
        case ASTTokenKind::BOOL:    lastExprType = currentScope->resolveType("bool"); break;
        // Was `&void`, which bought pointer assignability for free and cost
        // every diagnostic about it: `let x <int> = null` reported "got '&void'",
        // naming a pointer type in a program with no pointer in it. NullType is
        // assignable to every `T?` and every `&T` and to nothing else -- see
        // src/types/NullableType.hpp.
        case ASTTokenKind::KW_NULL: lastExprType = std::make_shared<NullType>(); break;
        default: lastExprType = nullptr;
    }
}

void SemanticAnalyzer::visit(Identifier& node) {
    // 1. Try local scope
    Symbol* sym = currentScope->resolve(node.name);
    if (sym) {
        lastExprType = sym->type;
        return;
    } 
    
    // 2. Try Implicit Field Access (self.name)
    if (currentStructContext) {
        // Use the helper to handle Self/Pointers
        auto st = std::dynamic_pointer_cast<StructType>(currentStructContext);
        if (!st) st = getStructType(currentStructContext, currentScope);

        if (st) {
            auto fieldType = st->getFieldType(node.name);
            if (fieldType) {
                lastExprType = fieldType;
                return;
            }
        }
    }
    
    error(node, "Undefined variable '" + node.name + "'");
    lastExprType = nullptr;
}

void SemanticAnalyzer::visit(BinaryOp& node) {
    node.left->accept(*this);
    auto leftType = lastExprType;
    
    node.right->accept(*this);
    auto rightType = lastExprType;
    
    if (!leftType || !rightType) {
        lastExprType = nullptr;
        return;
    }

    // Assignments
    bool isAssignment = (
        node.op == ASTTokenKind::EQUAL || 
        node.op == ASTTokenKind::PLUSEQUAL || 
        node.op == ASTTokenKind::MINUSEQUAL || 
        node.op == ASTTokenKind::MULTEQUAL || 
        node.op == ASTTokenKind::DIVEQUAL
    );

    if (isAssignment) {
        bool isLValue = false;
        if (dynamic_cast<Identifier*>(node.left.get())) isLValue = true;
        else if (dynamic_cast<MemberAccess*>(node.left.get())) isLValue = true;
        else if (dynamic_cast<ArrayAccess*>(node.left.get())) isLValue = true;
        else if (auto* unary = dynamic_cast<UnaryOp*>(node.left.get())) {
            if (unary->op == ASTTokenKind::MULT) isLValue = true;
        }
        
        if (!isLValue) error(node, "Invalid assignment target");
        
        if (auto* id = dynamic_cast<Identifier*>(node.left.get())) {
            auto* sym = currentScope->resolve(id->name);
            if (sym && !sym->is_mutable) {
                error(node, fmt::format("Cannot assign to immutable variable '{}'", id->name));
            }
        }

        checkType(*node.right, rightType, leftType);
        lastExprType = leftType;
        return;
    }

    // Operator Overloading. The *return* type: `operators` holds a whole signature
    // now, and a binary expression is typed by what its operator returns. Nothing
    // checks the right-hand operand against the operator's parameter yet -- an
    // operator call has no argument check at all, which is booked separately -- so
    // this reads only the half it always read.
    if (auto structType = getStructType(leftType, currentScope)) {
        if (auto retType = structType->getOperatorReturnType(static_cast<int>(node.op))) {
            lastExprType = retType;
            return;
        }
    }

    // Standard Primitives
    if (node.op == ASTTokenKind::AND || node.op == ASTTokenKind::OR) {
        auto boolType = currentScope->resolveType("bool");
        checkType(*node.left, leftType, boolType);
        checkType(*node.right, rightType, boolType);
        lastExprType = boolType;
        return;
    }

    if (node.op == ASTTokenKind::EQEQ || node.op == ASTTokenKind::NOTEQ ||
        node.op == ASTTokenKind::LT || node.op == ASTTokenKind::GT ||
        node.op == ASTTokenKind::LTEQ || node.op == ASTTokenKind::GTEQ) {
        // `0 == a` and `a == 0` are the same question, so a constant is looked for
        // on both sides. Without this the left operand is the expectation and a
        // constant on the left makes the *variable* the error: `blame 0 == a` for a
        // `uint` a reported "expected 'int', got 'uint'".
        //
        // Only constants are treated symmetrically. Whether two differently-typed
        // variables may be compared at all is a separate question, so when neither
        // side is a constant that fits, the check runs exactly as it did before and
        // reports at the same operand with the same message.
        // `x == null` is legal whatever x is, and so is `null == x`.
        // nullifier.fin:40 compares a *denullified* `_` -- a plain `int` by then --
        // with null, and the sample's own gloss (`assert @unpacked(_) == null`)
        // makes that the intended reading rather than a slip. stdlib/error.fin:12
        // does the same with a plain `int` parameter. Assignability is the wrong
        // question to ask about the one literal every type can be compared to.
        //
        // Only for `==` and `!=`. `x < null` falls through and is still reported,
        // because no sample orders anything against null and inventing an order
        // would be a ruling.
        const bool nullComparison = (node.op == ASTTokenKind::EQEQ || node.op == ASTTokenKind::NOTEQ)
                                    && (isNullLiteral(leftType) || isNullLiteral(rightType));

        if (!nullComparison &&
            !constantFitsType(*node.right, *leftType) &&
            !constantFitsType(*node.left, *rightType)) {
            checkType(*node.right, rightType, leftType);
        }
        lastExprType = currentScope->resolveType("bool");
        return;
    }

    if (!checkType(node, rightType, leftType)) {
        lastExprType = nullptr;
    } else {
        lastExprType = leftType;
    }
}

void SemanticAnalyzer::visit(UnaryOp& node) {
    node.operand->accept(*this);
    auto type = lastExprType;
    if (!type) return;

    if (node.op == ASTTokenKind::AMPERSAND) {
        lastExprType = std::make_shared<PointerType>(type);
    } 
    else if (node.op == ASTTokenKind::QUESTION) {
        // Postfix denullify -- nullifier.fin:31 `make_A(-1)?`, :42
        // `make_A(10)?.get_b()?`, undefined_behavior.fin:16 `add2()?`. It strips
        // exactly one level: `parser.y` gives it its own DENULLIFY precedence, and
        // the grammar admits no `T??` spelling for it to have to unwrap twice.
        //
        // The panic when the value *is* null is wave 4's -- there is no code
        // generator yet -- and it does not change the type either way, which is
        // why the whole runtime half of this operator is absent here.
        if (auto* nullable = type->as<NullableType>()) {
            lastExprType = nullable->inner;
        } else {
            // A `?` on something that was not nullable. nullifier.fin:36 says the
            // `any` case "should be an error", but `any` is not a resolved type at
            // all yet (docs/plan.md, Rulings owed), and no sample denullifies an
            // ordinary value. The identity, rather than a guess: a wrong error
            // here would be worse than a missing one, because it would reject
            // programs the corpus has not ruled on.
            lastExprType = type;
        }
    }
    else if (node.op == ASTTokenKind::MULT) {
        if (auto* ptr = dynamic_cast<const PointerType*>(type.get())) {
            lastExprType = ptr->pointee;
        } 
        else if (auto* ptrToArray = dynamic_cast<const PointerType*>(type.get())) {
            if (auto* arr = dynamic_cast<const ArrayType*>(ptrToArray->pointee.get())) {
                lastExprType = arr->clone(); 
                return;
            }
        }
        else {
            error(node, fmt::format("Cannot dereference non-pointer type '{}'", type->toString()));
            lastExprType = nullptr;
        }
    }
    else {
        lastExprType = type;
    }
}

// One check for every call that has a signature. Shared rather than copied because
// each of the three call sites got a different subset right: the function site had
// arity and argument types, the method site had neither, and the static-method site had
// neither but ordered its own two statements correctly. See the header for the ordering
// contract -- this walks the arguments, so the caller assigns the call's own type after.
void SemanticAnalyzer::checkCallArguments(ASTNode& node, const char* kind,
                                         const std::string& name,
                                         const FunctionType& sig,
                                         std::vector<std::unique_ptr<Expression>>& args) {
    size_t expected = sig.param_types.size();
    size_t actual = args.size();

    // A nullable parameter is optional at the call site. nullifier.fin:39 calls
    // `make_A()` with no arguments and says why: "since make_A says \"n?: int\" we
    // know that n can be null and we don't need to pass any arguments".
    //
    // The minimum is one past the *last* required parameter rather than the count
    // of required ones, because arguments bind positionally: `(a?: int, b: int)`
    // still needs both written, `(a: int, b?: int)` needs one. That falls out of
    // positional binding and needs no rule about which order the two kinds may
    // appear in -- which matters, because the corpus only ever writes the trailing
    // form and a rule invented here would be unratified.
    size_t required = 0;
    for (size_t i = 0; i < expected; ++i) {
        if (!sig.param_types[i]->as<NullableType>()) required = i + 1;
    }

    if (!sig.is_vararg && (actual < required || actual > expected)) {
        if (required == expected) {
            error(node, fmt::format("{} '{}' expects {} arguments, got {}", kind, name, expected, actual));
        } else {
            // "expects 2 arguments, got 0" is a lie about a function one of whose
            // parameters is optional, so the range is spelled out.
            error(node, fmt::format("{} '{}' expects between {} and {} arguments, got {}",
                                    kind, name, required, expected, actual));
        }
    }

    for (size_t i = 0; i < actual; ++i) {
        args[i]->accept(*this);
        if (i < expected) {
            checkType(*args[i], lastExprType, sig.param_types[i]);
        }
    }
}

void SemanticAnalyzer::visit(FunctionCall& node) {
    std::shared_ptr<FunctionType> funcType = nullptr;
    std::string funcName = node.name;

    // Case 1: Self(...)
    if (funcName == "Self") {
      if (!currentStructContext) {
            error(node, "'Self' used outside of struct");
            lastExprType = nullptr;
            return;
        }
        auto st = std::dynamic_pointer_cast<StructType>(currentStructContext);
        if (st && !st->constructors.empty()) {
            funcType = std::dynamic_pointer_cast<FunctionType>(st->constructors[0]);
        } else {
            error(node, "Struct '" + st->name + "' has no constructors");
            lastExprType = nullptr;
            return;
        }
    }
    // Case 2: Standard Function
    else {
        auto type = currentScope->resolveType(funcName);
        if (type) {
            if (auto st = getStructType(type, currentScope)) {
                if (!st->constructors.empty()) {
                    funcType = std::dynamic_pointer_cast<FunctionType>(st->constructors[0]);
                } else {
                     std::vector<std::shared_ptr<Type>> dummyParams;
                     funcType = std::make_shared<FunctionType>(dummyParams, st);
                }
            }
        } 
        else {
            Symbol* sym = currentScope->resolve(funcName);
            if (sym) {
                funcType = std::dynamic_pointer_cast<FunctionType>(sym->type);
            }
        }
    }

    if (!funcType) {
        error(node, "Undefined function or type '" + funcName + "'");
        lastExprType = nullptr;
        return;
    }

    checkCallArguments(node, "Function", funcName, *funcType, node.args);
    lastExprType = funcType->return_type;
}

void SemanticAnalyzer::visit(MethodCall& node) {
    node.object->accept(*this);
    auto objType = lastExprType;
    
    if (!objType) return; 

    // A call through a module qualifier: `stdio.printf("Big")`, complex.fin:14, whose
    // own comment says "uses stdio printf". Reading a module member already worked --
    // visit(MemberAccess&) has this branch and `stdio.nosuch` reports which module has
    // no such export -- but this function went straight to getStructType, which knows
    // nothing about namespaces, so a member could be named and not called and the two
    // spellings of the same mistake gave different diagnostics.
    //
    // The lookup and its failure message are deliberately the same as MemberAccess's, so
    // that `stdio.nosuch` and `stdio.nosuch()` agree; Soundness_Modules.AnUnknownModuleMemberIsReportedWhetherReadOrCalled
    // holds them together. What is *not* shared is the arity and argument checking, which
    // has to be reached explicitly -- a branch that resolved a member and returned its
    // return type without calling checkCallArguments would be a hole in the call checking
    // rather than a route into it.
    if (auto* ns = dynamic_cast<const NamespaceType*>(objType.get())) {
        Symbol* sym = ns->scope->resolve(node.method_name);
        if (!sym) {
            error(node, fmt::format("Namespace '{}' has no exported member '{}'", ns->name, node.method_name));
            lastExprType = nullptr;
            return;
        }
        auto funcType = std::dynamic_pointer_cast<FunctionType>(sym->type);
        if (!funcType) {
            // Resolved, but not to something callable. A struct lands here: its
            // constructors live on its StructType and this looks only in the value
            // table, so `stdio.IOError()` is refused --
            // KnownDefect_Modules.AModuleStructIsNotConstructibleThroughADot, which waits
            // on the same ruling as `let e <stdio.IOError>;` (a syntax error today, so a
            // module's types cannot be named in a type position at all).
            error(node, fmt::format("'{}.{}' is not a function", ns->name, node.method_name));
            lastExprType = nullptr;
            return;
        }
        checkCallArguments(node, "Function", ns->name + "." + node.method_name, *funcType, node.args);
        lastExprType = funcType->return_type;
        return;
    }

    auto structType = getStructType(objType, currentScope);

    if (!structType) {
    // The sentinel answers every access with itself, so one unresolved annotation
    // stays one diagnostic instead of becoming one per use. Without this the
    // cascade only changes shape -- measured before the sentinel existed:
    // suppressing `Undefined variable 'a'` alone just turned const.fin's nine
    // into five `does not have methods` and four `is not a struct`.
        if (isErrorType(objType)) { lastExprType = errorType(); return; }
        error(node, fmt::format("Type '{}' does not have methods", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    auto methodType = structType->getMethodType(node.method_name);
    if (!methodType) {
        error(node, fmt::format("Method '{}' not found in type '{}'", node.method_name, structType->name));
        lastExprType = nullptr;
        return;
    }

    // Assigned *after* the arguments are walked, and that is the whole of a bug this
    // unit found rather than set out to fix: `lastExprType = retType` used to come
    // first, so every argument overwrote the call's type and `s.m("x")` on a method
    // returning int was typed `string`. It reported a mismatch that did not exist and
    // accepted one that did. Soundness_MethodCalls.ACallsTypeIsItsReturnTypeNotIts-
    // LastArgument holds both halves.
    if (auto* sig = methodType->as<FunctionType>()) {
        checkCallArguments(node, "Method", node.method_name, *sig, node.args);
        lastExprType = sig->return_type;
    } else {
        // A `methods` entry that is not a signature: unreachable from the language
        // today, and an unchecked call rather than a crash if it ever is.
        for (auto& arg : node.args) arg->accept(*this);
        lastExprType = methodType;
    }
}

void SemanticAnalyzer::visit(ArrayAccess& node) {
    node.array->accept(*this);
    auto arrExprType = lastExprType;
    
    node.index->accept(*this);
    auto idxType = lastExprType;
    
    if (!arrExprType || !idxType) {
        lastExprType = nullptr;
        return;
    }

    // What the subscript has to be depends on what is being subscripted, so the
    // check comes after the shape is known and once per branch. It used to be a
    // single unconditional comparison against `int` up here, which made a
    // string-keyed prototype report twice -- once for not being an array, once for
    // a key that was never meant to be an integer. prototype_test.fin's note names
    // that second message.
    const auto intType = currentScope->resolveType("int");

    // A struct that declares `operator []` is subscripted through it, and that beats
    // both of the rules below: before this, a value receiver was reported as
    // `is not an array or pointer` even though it had declared the operator, and a
    // *pointer* receiver silently did pointer arithmetic and typed the subscript as
    // the pointee. getStructType unwraps pointers and Self for us, so both spellings
    // arrive here. tests/samples/deeptest4.fin:13-17 and stdlib/hashmap.fin:39 are
    // the five corpus sites; lib/std/collection.fin:92 and lib/std/hashmap.fin:84
    // declare the operator this consults.
    //
    // Checked before the pointer unwrap below so that `&Map` reaches the operator
    // rather than being read as an offset into an array of Maps -- a struct that
    // declares a subscript means the subscript.
    if (auto structType = getStructType(arrExprType, currentScope)) {
        const int indexOp = static_cast<int>(ASTTokenKind::INDEX);
        if (auto opType = structType->getOperatorType(indexOp)) {
            if (auto* sig = opType->as<FunctionType>()) {
                // One parameter is what `operator [](i: <int>)` declares. A signature
                // with none is not a subscript at all, so the index goes unchecked
                // rather than being compared against nothing.
                if (!sig->param_types.empty()) checkType(*node.index, idxType, sig->param_types[0]);
                lastExprType = sig->return_type;
                return;
            }
            // A registration that is not a signature: unreachable today, since every
            // defineOperator call builds a FunctionType. Typed as whatever is there
            // rather than dropped, and the index left unchecked, for the same reason
            // the method-call site tolerates the same shape.
            lastExprType = opType;
            return;
        }
    }

    // A pointer to an array is indexed as the array it points at. A pointer to
    // anything else is indexed as pointer arithmetic and yields its pointee, so the
    // subscript is an offset and therefore an int.
    if (auto* ptrToArray = dynamic_cast<const PointerType*>(arrExprType.get())) {
        if (dynamic_cast<const ArrayType*>(ptrToArray->pointee.get())) {
            arrExprType = ptrToArray->pointee;
        } else {
            checkType(*node.index, idxType, intType);
            lastExprType = ptrToArray->pointee;
            return;
        }
    }

    // A prototype is subscripted by its key and yields its value.
    // stdlib/memory.fin:30-33 declares `let info <{string, string}>;` and then
    // writes `info["MemoryCardModel"] = <a string>`; prototype_test.fin:17's comment
    // says the same for a read -- "a_member will be 10 because of the key, value
    // (10, 10)". Nothing here refuses a key that is not in the prototype yet:
    // :20's comment ("a would be {10:10, "a":true,"b":false} after this statement")
    // makes growing one by writing to a new key the way a prototype is filled, and
    // this visitor cannot tell a read from a write anyway.
    if (auto* proto = dynamic_cast<const PrototypeType*>(arrExprType.get())) {
        checkType(*node.index, idxType, proto->keyType);
        lastExprType = proto->valueType;
        return;
    }

    if (auto* arrType = dynamic_cast<const ArrayType*>(arrExprType.get())) {
        checkType(*node.index, idxType, intType);
        lastExprType = arrType->element_type;
    } else if (isErrorType(arrExprType)) {
        lastExprType = errorType();  // see the note at the method-call site
    } else {
        // Deliberately without an index check: the subscript of something that
        // cannot be subscripted has no expected type to be wrong against, and a
        // second message about it would be noise attached to the same mistake. The
        // index expression was still walked above, so anything undefined inside it
        // has already been reported.
        error(node, fmt::format("Type '{}' is not an array or pointer", arrExprType->toString()));
        lastExprType = nullptr;
    }
}

void SemanticAnalyzer::visit(MacroCall& node) {
    // Macro calls should be expanded before semantic analysis.
    // If we reach here, it's either unexpanded or inside a quote.
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    lastExprType = nullptr; // Or a placeholder type if we want to support unexpanded macros
}

void SemanticAnalyzer::visit(MacroInvocation& node) {
    // Similar to MacroCall
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    lastExprType = nullptr;
}

void SemanticAnalyzer::visit(TypeLiteralExpression& node) {
    // The body is analysed through the same visit a named declaration takes, which
    // is what makes a field's default and a method's body checked here without a
    // second copy of that logic. literal_struct.fin:24 and literal_interface.fin:20.
    //
    // Inside a scope of its own. `visit(StructDeclaration&)` defines the type by
    // name in `currentScope`, and the name here is generated -- so without this the
    // enclosing function would gain a type nobody can spell, and it would gain one
    // per literal. Discarding the scope also settles what a literal's members are
    // visible to: the type itself, and nothing outside it.
    //
    // The value is the meta-type, and it is set after the body walk rather than
    // before, because analysing the body overwrites lastExprType.
    enterScope();
    node.decl->accept(*this);
    exitScope();

    lastExprType = currentScope->resolveType(node.is_interface ? "$interface" : "$struct");
}

void SemanticAnalyzer::visit(CastExpression& node) {
    node.expr->accept(*this);
    auto sourceType = lastExprType;
    auto targetType = resolveTypeFromAST(node.target_type.get());
    
    if (!sourceType || !targetType) {
        lastExprType = nullptr;
        return;
    }

    // The fourth expression site, and the one that leaked: `cast<float>(x)` where x's
    // annotation did not resolve said `Invalid cast from '<error>' to 'float'` -- a
    // cascade *and* a diagnostic naming a type no program can write.
    //
    // The result is the target type, not the sentinel. A cast is an assertion about
    // the value's type, and that assertion stands whether or not the operand typed:
    // `let s <string> = cast<float>(x);` should still say string got float, which is
    // true of what was written. The sentinel would swallow that too.
    if (isErrorType(sourceType)) {
        lastExprType = targetType;
        return;
    }

    bool valid = false;
    if (sourceType->equals(*targetType)) valid = true;
    else if (dynamic_cast<const PrimitiveType*>(sourceType.get()) && 
             dynamic_cast<const PrimitiveType*>(targetType.get())) valid = true;
    else if (dynamic_cast<const PointerType*>(sourceType.get()) && 
             dynamic_cast<const PointerType*>(targetType.get())) valid = true;
    else if (dynamic_cast<const GenericType*>(sourceType.get()) || 
             dynamic_cast<const GenericType*>(targetType.get())) valid = true;
    // A dynamic type on either side. Casting *out* of one is the whole point of having
    // one -- nullifier.fin:12 writes `let b <int> = cast<int>(a);` where `a` is `any`,
    // and stdlib/types.fin:33 declares `fun cast_to<T>(value: any) -> T` whose body can
    // only be that cast. Casting *into* one is how a value enters, and the corpus site
    // survived by accident: `cast_to`'s target is the generic parameter `T`, which the
    // arm above already admits, so the corpus never exercised the dynamic target and
    // the standard library's own signature was what would have shown the defect.
    //
    // Unchecked in both directions, deliberately. A cast is the program overriding the
    // checker; `cast<int>(x)` where x is `any` is the programmer asserting what the box
    // holds, and there is nothing at compile time to verify that against -- that is
    // what makes it a cast and not an assignment. The run-time check belongs to
    // codegen, which will need a tag to compare and does not have one yet.
    else if (dynamic_cast<const DynamicType*>(sourceType.get()) ||
             dynamic_cast<const DynamicType*>(targetType.get())) valid = true;
    
    if (!valid) {
        error(node, fmt::format("Invalid cast from '{}' to '{}'", sourceType->toString(), targetType->toString()));
        lastExprType = nullptr;
    } else {
        lastExprType = targetType;
    }
}

void SemanticAnalyzer::visit(NewExpression& node) {
    for(auto& arg : node.args) arg->accept(*this);
    for(auto& f : node.init_fields) f.second->accept(*this);
    
    auto allocatedType = resolveTypeFromAST(node.type.get());
    // A PointerType over a null pointee is worse than no type at all: it is
    // non-null, so every `if (!type) return;` downstream lets it through, and the
    // first toString() -- checkType's own error message, usually -- dereferences
    // the null. `new Nope{}` died in PointerType.cpp:6 that way. resolveTypeFromAST
    // has already reported why it failed, so there is nothing to add here.
    lastExprType = allocatedType ? std::make_shared<PointerType>(allocatedType) : nullptr;
}

void SemanticAnalyzer::visit(MemberAccess& node) {
    node.object->accept(*this);
    auto objType = lastExprType;
    if (!objType) return;

    if (auto* ns = dynamic_cast<const NamespaceType*>(objType.get())) {
        Symbol* sym = ns->scope->resolve(node.member);
        if (sym) {
            lastExprType = sym->type;
            return;
        }
        error(node, fmt::format("Namespace '{}' has no exported member '{}'", ns->name, node.member));
        lastExprType = nullptr;
        return;
    }

    auto structType = getStructType(objType, currentScope);

    if (!structType) {
        if (isErrorType(objType)) { lastExprType = errorType(); return; }  // see the method-call site

        // Reached only once struct resolution has already failed, which is what makes
        // "a declared field named `length` outranks the builtin" true by construction
        // rather than by an ordering that a later edit could reverse. lib/std/collection.fin
        // has a `length` field and reads it eight times, so the two must not compete;
        // Soundness_BuiltinMembers.AStructFieldNamedLengthIsNotTheBuiltin pins it.
        if (typeHasBuiltinMembers(objType)) {
            // An `int`, on an array and on a string both. Forced, not chosen: Fin
            // converts between no two integer types, so whatever width a length has is
            // the only width it can be compared against, and all five corpus sites
            // compare one against an `int` (`array.length <= 1`, `i < a.length - 1` with
            // `i: int`, `path.length == 10`). A wider length would convict four of them
            // on the day it landed. ALengthIsAnIntAndNotAnotherIntegerWidth is the test
            // that makes a later widening ruling come past a red assertion.
            if (node.member == "length") {
                lastExprType = currentScope->resolveType("int");
                return;
            }
            error(node, fmt::format("Type '{}' has no member '{}'", objType->toString(), node.member));
            lastExprType = nullptr;
            return;
        }

        error(node, fmt::format("Type '{}' is not a struct", objType->toString()));
        lastExprType = nullptr;
        return;
    }

    auto fieldType = structType->getFieldType(node.member);
    if (!fieldType) {
        error(node, fmt::format("Struct '{}' has no member '{}'", structType->name, node.member));
        lastExprType = nullptr;
    } else {
        bool isPublic = structType->isFieldPublic(node.member);
        bool isInternal = false;
        if (currentStructContext && currentStructContext->equals(*structType)) isInternal = true;
        
        if (!isPublic && !isInternal) {
            error(node, fmt::format("Cannot access private field '{}' of struct '{}'", node.member, structType->name));
        }

        lastExprType = fieldType;
    }
}

void SemanticAnalyzer::visit(StructInstantiation& node) {
    auto baseType = currentScope->resolveType(node.struct_name);
    if (!baseType) {
        error(node, "Undefined struct '" + node.struct_name + "'");
        lastExprType = nullptr;
        return;
    }
    
    auto structDef = std::dynamic_pointer_cast<StructType>(baseType);
    if (!structDef) {
        error(node, "'" + node.struct_name + "' is not a struct");
        lastExprType = nullptr;
        return;
    }

    std::shared_ptr<StructType> concreteType = structDef;

    if (!node.generic_args.empty()) {
        std::vector<std::shared_ptr<Type>> args;
        for (auto& arg : node.generic_args) {
            auto t = resolveTypeFromAST(arg.get());
            if (t) args.push_back(t);
        }
        
        auto instantiated = structDef->instantiate(args);
        if (!instantiated) {
            error(node, "Generic count mismatch in struct instantiation");
            lastExprType = nullptr;
            return;
        }
        concreteType = std::static_pointer_cast<StructType>(instantiated);
    }
    
    lastExprType = concreteType;

    for(auto& f : node.fields) {
        f.second->accept(*this);
        auto exprType = lastExprType; 
        auto fieldType = concreteType->getFieldType(f.first);
        
        if (!fieldType) {
            error(node, fmt::format("Struct '{}' has no field '{}'", concreteType->toString(), f.first));
        } else {
            checkType(*f.second, exprType, fieldType);
        }
    }
    
    lastExprType = concreteType;
}

void SemanticAnalyzer::visit(ArrayLiteral& node) {
    if (node.elements.empty()) {
        error(node, "Empty array literal cannot infer type.");
        lastExprType = nullptr;
        return;
    }

    // `if (!firstType) return;` stood here and it suppressed too much: the return
    // skipped the loop below, so `[n1, n2]` reported n1 and never looked at n2. A
    // cascade and a skipped walk are one diagnostic apart and are opposites -- the
    // first drops a message that says nothing new, the second drops a message about a
    // different mistake.
    //
    // The sentinel does both jobs. Every element is still visited, and each one is
    // compared against `<error>`, which checkType absorbs.
    node.elements[0]->accept(*this);
    auto firstType = lastExprType ? lastExprType : errorType();

    for (size_t i = 1; i < node.elements.size(); ++i) {
        node.elements[i]->accept(*this);
        checkType(*node.elements[i], lastExprType, firstType);
    }

    lastExprType = std::make_shared<ArrayType>(firstType, true);
}

void SemanticAnalyzer::visit(SizeofExpression& node) {
    if (node.type_target) {
        resolveTypeFromAST(node.type_target.get());
    } else if (node.expr_target) {
        node.expr_target->accept(*this);
    }
    lastExprType = currentScope->resolveType("int");
}

void SemanticAnalyzer::visit(LambdaExpression& node) {
    // The scope is entered before the signature is resolved, and the lambda's own
    // type parameters are registered in it first, because `<T>(m: T) <T> => m`
    // declares T and then immediately uses it: resolving the signature outside
    // the scope left T undefined in the one place it is introduced, and the
    // parameter it typed undefined in the body. This mirrors what
    // visit(FunctionDeclaration&) does at Analyzer_Decl.cpp:43.
    enterScope();

    declareGenericParams(node.generic_params);

    std::shared_ptr<Type> retType = nullptr;
    if (node.return_type) {
        retType = resolveTypeFromAST(node.return_type.get());
    } else {
        retType = currentScope->resolveType("void"); 
    }

    std::vector<std::shared_ptr<Type>> paramTypes;
    for(auto& param : node.params) {
        auto t = resolveTypeFromAST(param->type.get());
        if(t) {
            currentScope->define({param->name, t, false, true});
            paramTypes.push_back(t);
        }
    }
    
    auto prevRet = context.currentFuncReturnType;
    context.currentFuncReturnType = retType;
    
    if (node.body) {
        node.body->accept(*this);
    } else if (node.expression_body) {
        node.expression_body->accept(*this);
        if (lastExprType) {
            checkType(*node.expression_body, lastExprType, retType);
        }
    }
    
    context.currentFuncReturnType = prevRet;
    exitScope();
    
    // Not when the return type did not resolve: FunctionType dereferences it in
    // toString(), and nullptr already means "unknown, stop asking" to every
    // reader of lastExprType.
    lastExprType = retType ? std::make_shared<FunctionType>(paramTypes, retType) : nullptr;
}

void SemanticAnalyzer::visit(QuoteExpression& node) {
    if (node.block) node.block->accept(*this);
    lastExprType = currentScope->resolveType("auto");
}

void SemanticAnalyzer::visit(TernaryOp& node) {
    node.condition->accept(*this);
    node.true_expr->accept(*this);
    auto t = lastExprType;
    node.false_expr->accept(*this);
    auto f = lastExprType;
    if (t && f) {
        // A ternary has no expected branch either. When one branch is an integer
        // constant it takes the other branch's type, and the *other* branch's type
        // is the result -- otherwise `true : 1 ? a` for a `uint` a would report the
        // variable as the error and then hand `int` to whatever consumes it, which
        // is two wrong diagnostics from one constant.
        if (constantFitsType(*node.false_expr, *t)) {
            lastExprType = t;
        } else if (constantFitsType(*node.true_expr, *f)) {
            lastExprType = f;
        } else {
            checkType(*node.false_expr, f, t);
            lastExprType = t;
        }
    }
}

void SemanticAnalyzer::visit(StaticMethodCall& node) {
    // 1. Resolve Target Type (e.g. Vec2<float>)
    auto type = resolveTypeFromAST(node.target_type.get());
    if (!type) {
        lastExprType = nullptr;
        return;
    }

    // 2. Unwrap Self/Pointers to get StructType
    auto structType = getStructType(type, currentScope);
    if (!structType) {
        error(node, fmt::format("Type '{}' is not a struct", type->toString()));
        lastExprType = nullptr;
        return;
    }

    // 3. Look up Method
    
    auto methodType = structType->getMethodType(node.method_name);
    if (!methodType) {
        error(node, fmt::format("Static method '{}' not found in '{}'", node.method_name, structType->toString()));
        lastExprType = nullptr;
        return;
    }

    // 4. Analyze Args
    //
    // Against the same stored signature as an instance call, which is the signature as
    // *called*: the receiver is not in it. Calling an instance method through `::` and
    // passing the receiver by hand would therefore be one argument over -- no corpus
    // sample does it (the four `::` calls are prototype_test.fin:27, :30,
    // stdlib/stdio.fin:153 and nullifier.fin:26, all static), so which of the two
    // spellings that is stays an owner question rather than a rule invented here.
    if (auto* sig = methodType->as<FunctionType>()) {
        checkCallArguments(node, "Static method", node.method_name, *sig, node.args);
        lastExprType = sig->return_type;
    } else {
        for (auto& arg : node.args) arg->accept(*this);
        lastExprType = methodType;
    }
}

void SemanticAnalyzer::visit(SuperExpression& node) {
    std::shared_ptr<Type> parentType = nullptr;
    
    if (auto st = std::dynamic_pointer_cast<StructType>(currentStructContext)) {
        if (!node.parent_name.empty()) {
            for(auto& p : st->parents) {
                if (p->toString() == node.parent_name) {
                    parentType = p;
                    break;
                }
            }
            if (!parentType) parentType = currentScope->resolveType(node.parent_name);
        } else {
            if (!st->parents.empty()) {
                parentType = st->parents[0];
            }
        }
    }

    if (!parentType) {
        error(node, "Cannot resolve 'super' (no parent found)");
        lastExprType = nullptr;
        return;
    }

    if (!node.init_fields.empty()) {
        for (auto& f : node.init_fields) f.second->accept(*this);
    } else {
        for (auto& arg : node.args) arg->accept(*this);
    }

    lastExprType = parentType;
}



} // namespace fin
