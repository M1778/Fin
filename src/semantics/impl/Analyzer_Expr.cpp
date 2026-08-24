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

// Can a value be stored into this expression?
//
// Shared by assignment and by `++`/`--`, which is the point: `i = i + 1` and `i++`
// are the same mutation, and they disagreed about what was allowed to be on the left
// of it. Assignment checked; `++` did not check at all, so `5++` and `(a + b)++` were
// accepted and the analyzer handed the backend an increment with nowhere to put the
// result.
//
// A dereference is a target because `*p = 1` is one. An index is a target because
// `array[i] = x` is one (arrays.fin:20). A call is not, and neither is any operator
// other than `*` -- `(a + b)` names no storage, and a struct returned by value from
// `make(5)` has nowhere for `make(5).a++` to write back to.
bool isAssignableTarget(const Expression* expr) {
    if (!expr) return false;
    if (dynamic_cast<const Identifier*>(expr)) return true;
    if (dynamic_cast<const MemberAccess*>(expr)) return true;
    if (dynamic_cast<const ArrayAccess*>(expr)) return true;
    if (auto* unary = dynamic_cast<const UnaryOp*>(expr)) {
        return unary->op == ASTTokenKind::MULT;
    }
    return false;
}

// Is `++` meaningful on this type? Only where "add one" is.
//
// Integers and floats, and nothing else. Not a bool: there is no bool one greater
// than `true`, and C++ removed `b++` for saying otherwise. Not a string, even though
// `+` on two strings concatenates -- `s + 1` has a number on one side and no meaning.
// Not a pointer: whether `p++` advances by one element or one byte is an owner ruling
// nothing in the corpus needs, and guessing it wrong is a silent out-of-bounds rather
// than a diagnostic.
//
// An unresolved or error type answers *true*, so that a name that already produced a
// diagnostic does not produce a second one about its increment.
bool isIncrementable(const TypePtr& type) {
    if (!type || isErrorType(type)) return true;
    auto* prim = dynamic_cast<const PrimitiveType*>(type.get());
    if (!prim) return false;
    const std::string& n = prim->name;
    return n == "int" || n == "long" || n == "short" || n == "char" || n == "uint" ||
           n == "ulong" || n == "ushort" || n == "float" || n == "double";
}

// `v.0`: is this member name a position rather than a name?
//
// The grammar makes the digits of `expression DOT INTEGER` the member name
// (parser.y:2258), and a declared field cannot collide with them -- an identifier must
// start with a letter or an underscore. So "every character is a digit" is the whole
// test, and a name that is not one is not positional whatever else it is.
//
// The bound is on the *spelling*: nine digits fit any position a payload or a
// prototype could have, and a longer one is out of range rather than an overflow.
static bool positionalMember(const std::string& name, size_t& index) {
    if (name.empty() || name.size() > 9) return false;
    for (char c : name) if (c < '0' || c > '9') return false;
    index = static_cast<size_t>(std::stoul(name));
    return true;
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

    // Fixed and dynamic alike. ArrayType carries an extent and nothing here reads it,
    // because a count is a count either way; a fixed array's `.length` could be folded
    // to its extent later without changing its type.
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

// Is this type an enum? Not "is it struct-shaped" -- an enum's type is a StructType,
// which is the whole hazard StructType.hpp's two-flags rule warns about, so a reader
// that means "an enum" has to say `is_enum` and a reader that means "a struct" has to
// say `!is_enum`. Written out here because the dynamic_pointer_cast plus the flag test
// is exactly the pair that is easy to write as just the cast, and just the cast is the
// bug.
//
// One caller today: the cast rule below, which holds a TypePtr and wants a yes or no.
// The two other sites that ask the same question -- the enumerator lookup in
// visit(MemberAccess&) and the refusal in visit(StructInstantiation&) -- already hold
// the StructType and go on to use it, so they test the flag directly rather than throw
// the pointer away and ask for it again.
//
// No unwrapping, deliberately unlike getStructType above: a `&State` is a pointer, and
// the caller asks about a value. If a reader ever needs the question through a pointer,
// that reader should say so at its own call site.
static bool isEnumType(const TypePtr& type) {
    auto asStruct = std::dynamic_pointer_cast<StructType>(type);
    return asStruct && asStruct->is_enum;
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

// Whether `name`, holding `t`, is an enumerator of the enum `t` points at.
//
// The two spellings a member's name can have are both accepted: a payloadless
// member is typed as its enum, and a payloaded one as the constructor that builds
// it (StructType::getEnumeratorValueType). Either way the enum is reachable, and
// asking it whether it declares this name is what separates a member from a value
// of the same type -- `let f <Flag> = On;` gives `f` exactly the type `On` has, and
// only one of the two names is a member.
static bool namesAnEnumerator(const std::string& name, const std::shared_ptr<Type>& t) {
    if (!t) return false;
    std::shared_ptr<StructType> owner;
    if (auto* sig = t->as<FunctionType>())
        owner = std::dynamic_pointer_cast<StructType>(sig->return_type);
    else
        owner = std::dynamic_pointer_cast<StructType>(t);
    return owner && owner->is_enum && owner->getEnumerator(name) != nullptr;
}

void SemanticAnalyzer::visit(Identifier& node) {
    // 1. Try local scope
    Symbol* sym = currentScope->resolve(node.name);
    if (sym) {
        // An enumerator's *name*, where a `$enum_member` is expected, is that member.
        //
        // lib/std/enums.fin:29 declares `keyidof(enum_member: $enum_member)` and says
        // beside it what the argument is: "the argument is an enum *member*, not a
        // value of the enum, which is what the `$enum_member` meta-type is for".
        // tests/samples/stdlib/typing.fin calls it on 29, 37 and 43, and each was
        // `expected '$enum_member', got 'fn(T) -> Result<T, U>'` -- a member named
        // rather than called is its constructor, and a constructor is not a member.
        //
        // Read before the symbol's own type, unlike the `$type` rule below, because
        // the name resolves either way: `Ok` is in the value scope. What is expected
        // is still what decides, so `Ok` is its constructor everywhere else
        // (Soundness_EnumMemberValue).
        if (auto hint = hintFor(node)) {
            if (auto* prim = hint->as<PrimitiveType>()) {
                if (prim->name == "$enum_member" && namesAnEnumerator(node.name, sym->type)) {
                    lastExprType = hint;
                    return;
                }
            }
        }
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
    
    // 3. A type's name, where a `$type` is what is expected.
    //
    // tests/samples/stdlib/error.fin:27 writes `compiler.types.cmp_types(t, Error)`
    // inside `@special(pub) is_error_type( t: $type )`, naming the struct its own
    // line 8 declares. `cmp_types` takes two `$type` arguments, so the name is a
    // type used as a value -- "$type == literal type", as stdlib/types.fin:33 puts
    // it.
    //
    // Read last, so a variable and a field of that name both still win, and read
    // only under the expectation. A general rule -- any type name is a value
    // anywhere -- would cost two things the corpus can point at: a misspelled
    // variable that collides with a type name would stop being reported as
    // undefined, and enums.fin:26's `enum_ == Ok(T)` means "is this the Ok
    // variant", so turning its `T` into a `$type` would answer a question that
    // line is not asking. Soundness_TypeAsValue holds both halves.
    //
    // Only `$type`. Whether `$interface`, `$struct` and `$enum_member` accept a
    // name the same way, and whether they check the named type's kind if they do,
    // is unsettled -- the corpus writes none of the three, and `implements(bool;
    // $type, $interface)` is the only member that would ask.
    //
    // The identity of the type is not carried: the expression's type is `$type`
    // and not "the type Error". Nothing needs it yet -- `cmp_types` answers at
    // compile time, and `gettype::<T>` takes its subject through the turbofish,
    // where identity travels as a type argument rather than as a value.
    if (auto hint = hintFor(node)) {
        if (auto* prim = hint->as<PrimitiveType>()) {
            if (prim->name == "$type" && currentScope->resolveType(node.name)) {
                lastExprType = hint;
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

    // Assignments
    bool isAssignment = (
        node.op == ASTTokenKind::EQUAL || 
        node.op == ASTTokenKind::PLUSEQUAL || 
        node.op == ASTTokenKind::MINUSEQUAL || 
        node.op == ASTTokenKind::MULTEQUAL || 
        node.op == ASTTokenKind::DIVEQUAL
    );

    // The target's type is a hint for the value, which is what a declaration's
    // annotation is: `r = Err("Blame ME!");` (tests/samples/enums.fin:47) is the same
    // statement as `let r <Result<int, string>> = Err("Blame ME!");` minus the place to
    // write the type, and the variable already carries it. Plain `=` only -- a compound
    // assignment's right-hand side is an operand and not the whole value.
    //
    // This is why the right operand is walked here rather than beside the left: the hint
    // has to be in place before the expression it applies to is visited.
    if (isAssignment && node.op == ASTTokenKind::EQUAL && leftType && !isErrorType(leftType)) {
        typeHintFor = node.right.get();
        typeHint = leftType;
    }
    node.right->accept(*this);
    typeHintFor = nullptr;
    typeHint = nullptr;
    auto rightType = lastExprType;

    if (!leftType || !rightType) {
        lastExprType = nullptr;
        return;
    }

    if (isAssignment) {
        if (!isAssignableTarget(node.left.get())) error(node, "Invalid assignment target");
        
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

    if (node.op == ASTTokenKind::INCREMENT || node.op == ASTTokenKind::DECREMENT) {
        // `i++` writes to `i`, so it is checked as the assignment it is. Before this
        // the whole operator fell through to `lastExprType = type`: `const c <int> =
        // 1; c++;` was accepted on the line after `c = 2;` was refused, the same
        // mutation through two spellings with one of them unguarded.
        const char* verb = node.op == ASTTokenKind::INCREMENT ? "increment" : "decrement";
        if (!isAssignableTarget(node.operand.get())) {
            error(node, fmt::format("Invalid {} target", verb));
        } else if (auto* id = dynamic_cast<Identifier*>(node.operand.get())) {
            // Only a named variable's mutability is checked, which is what assignment
            // checks too: a member assignment is never mutability-checked, and that
            // defect is booked rather than half-fixed here for one operator.
            auto* sym = currentScope->resolve(id->name);
            if (sym && !sym->is_mutable) {
                error(node, fmt::format("Cannot {} immutable variable '{}'", verb, id->name));
            }
        }
        if (!isIncrementable(type)) {
            error(node, fmt::format("Cannot {} a value of type '{}'", verb, type->toString()));
        }
        // The type either way: `let n <int> = i++;` is an int, and reporting the
        // operand's type after refusing the operator keeps one diagnostic to one
        // mistake instead of cascading into the declaration it initialises.
        lastExprType = type;
    }
    else if (node.op == ASTTokenKind::AMPERSAND) {
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

namespace {

// Does this type still mention a generic parameter that has not been given a value?
//
// The gate on generic-argument inference, and deliberately narrow: it looks at a
// struct's generic_args and not at its fields, so `Box<int>` is concrete even though
// `Box<T>::v` was written `T` -- the field is already substituted by the time an
// instantiated struct exists, and reading fields here would call a fully applied type
// generic and re-infer it.
bool mentionsGenericParam(const TypePtr& t) {
    if (!t) return false;
    if (t->as<GenericType>()) return true;
    if (auto* p = t->as<PointerType>()) return mentionsGenericParam(p->pointee);
    if (auto* a = t->as<ArrayType>()) return mentionsGenericParam(a->element_type);
    if (auto* n = t->as<NullableType>()) return mentionsGenericParam(n->inner);
    if (auto* s = t->as<StructType>()) {
        for (const auto& g : s->generic_args) if (mentionsGenericParam(g)) return true;
        return false;
    }
    if (auto* pr = t->as<PrototypeType>())
        return mentionsGenericParam(pr->keyType) || mentionsGenericParam(pr->valueType);
    if (auto* f = t->as<FunctionType>()) {
        if (mentionsGenericParam(f->return_type)) return true;
        for (const auto& q : f->param_types) if (mentionsGenericParam(q)) return true;
    }
    return false;
}

// Learn what a generic parameter stands for by matching a written argument against the
// parameter it binds to, structurally. Nothing is diagnosed here -- an argument that
// does not match the shape teaches nothing and is left to checkType, which runs after
// against the instantiated parameters and already has the message for it.
//
// First binding wins. `Pair(1, "x")` for `Pair(a: A, b: A)` therefore instantiates
// `Pair<int, int>` and reports the second argument as a type mismatch, rather than
// needing a second diagnostic about conflicting inferences that no sample asks for.
void unifyGeneric(const TypePtr& param, const TypePtr& arg, TypeMap& out) {
    if (!param || !arg) return;
    if (auto* g = param->as<GenericType>()) {
        if (!out.count(g->name)) out[g->name] = arg;
        return;
    }
    if (auto* p = param->as<PointerType>()) {
        if (auto* pa = arg->as<PointerType>()) unifyGeneric(p->pointee, pa->pointee, out);
        return;
    }
    if (auto* a = param->as<ArrayType>()) {
        // The extent is not matched. `rptr([1,2,3,4])` (tests/samples/const.fin:98)
        // hands a four-element literal to a parameter written `[T]`, and the size is
        // not part of what T is -- if it were, T would bind to `[int, 4]` and the
        // `rptr<[int]>` annotation on that line would not fit it.
        if (auto* aa = arg->as<ArrayType>()) unifyGeneric(a->element_type, aa->element_type, out);
        return;
    }
    if (auto* n = param->as<NullableType>()) {
        // `n?: T` given 5 learns T = int: a nullable parameter accepts the bare value,
        // so the same pairing has to teach the same thing (nullifier.fin's optional
        // parameters are what makes this reachable at all).
        if (auto* na = arg->as<NullableType>()) unifyGeneric(n->inner, na->inner, out);
        else unifyGeneric(n->inner, arg, out);
        return;
    }
    // `Self` on a template stands for the enclosing struct, so matching `&Self`
    // against `&Vec2<float>` (tests/samples/letssee.fin:73) is matching `Vec2<T>`
    // against it -- which is what says T is float. Without this arm the one static call
    // in the corpus with an informative argument teaches nothing, and a `<Self>` return
    // type cannot be seeded from an annotation either (prototype_test.fin:27).
    if (auto* self = param->as<SelfType>()) {
        unifyGeneric(self->originalStruct, arg, out);
        return;
    }
    if (auto* s = param->as<StructType>()) {
        if (auto* sa = arg->as<StructType>()) {
            if (s->name == sa->name && s->generic_args.size() == sa->generic_args.size()) {
                for (size_t i = 0; i < s->generic_args.size(); ++i)
                    unifyGeneric(s->generic_args[i], sa->generic_args[i], out);
            }
        }
        return;
    }
    if (auto* pr = param->as<PrototypeType>()) {
        if (auto* pa = arg->as<PrototypeType>()) {
            unifyGeneric(pr->keyType, pa->keyType, out);
            unifyGeneric(pr->valueType, pa->valueType, out);
        }
        return;
    }
    if (auto* f = param->as<FunctionType>()) {
        if (auto* fa = arg->as<FunctionType>()) {
            for (size_t i = 0; i < f->param_types.size() && i < fa->param_types.size(); ++i)
                unifyGeneric(f->param_types[i], fa->param_types[i], out);
            unifyGeneric(f->return_type, fa->return_type, out);
        }
        return;
    }
}

// What a struct's generic arguments became, in the order the struct declares them.
//
// StructType::instantiate takes a positional list, and a mapping learned from arguments
// is by name and may be missing entries -- `G::g()` learns nothing at all. An entry
// nothing bound keeps the parameter it already had, so substituting it is a no-op and
// the struct stays the template rather than becoming a struct with fewer arguments than
// it declares.
std::vector<TypePtr> orderedGenericArgs(const std::shared_ptr<StructType>& st, const TypeMap& mapping) {
    std::vector<TypePtr> out;
    for (const auto& g : st->generic_args) {
        auto it = mapping.find(g->toString());
        out.push_back(it != mapping.end() ? it->second : g);
    }
    return out;
}

} // namespace

// Arity only, so that the generic-inference path can report it before it walks the
// arguments -- see the header.
void SemanticAnalyzer::checkCallArity(ASTNode& node, const char* kind,
                                     const std::string& name,
                                     const FunctionType& sig, size_t actual) {
    size_t expected = sig.param_types.size();

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
    checkCallArity(node, kind, name, sig, args.size());

    size_t expected = sig.param_types.size();
    for (size_t i = 0; i < args.size(); ++i) {
        // The fourth thing that says what a value is about to become: the parameter
        // this argument is passed to. The other three -- an annotation, an
        // assignment target, a return's function -- are on typeHint in the header,
        // and the contract there is that nothing distinguishes them once installed.
        //
        // Installed before the argument is walked, because that is when it has to be
        // there, and keyed on the argument node so it does not reach inward: the `S`
        // of `cmp_types(t, id(S))` is `id`'s argument and not this one's
        // (Soundness_TypeAsValue.TheExpectationDoesNotReachASubexpression).
        if (i < expected) {
            typeHintFor = args[i].get();
            typeHint = sig.param_types[i];
        }
        args[i]->accept(*this);
        typeHintFor = nullptr;
        typeHint = nullptr;
        if (i < expected) {
            checkType(*args[i], lastExprType, sig.param_types[i]);
        }
    }
}

// The same check for a call whose callee still mentions a generic parameter, and the
// call's own type as the return value.
//
// A call cannot use checkCallArguments when the parameters are not known yet: the
// argument types are what say what the parameters are, so they have to be walked before
// anything can be checked against them. What that costs is arity ordering, which
// checkCallArity is called for explicitly here so that `Box(1, 2)` still reports its
// arity ahead of anything its arguments say.
//
// Three things say what the parameters are and they are read in this order:
//
//   `seed`      what the call wrote for itself: `Box::<int>()`. Nothing else can
//               disagree with it -- it is a statement about the type rather than an
//               implication drawn from a value.
//   the hint    an annotation, an assignment target, or the enclosing function's
//               declared return type -- whatever the call's *result* is known to be.
//               First, so that it wins: `let arr <rptr<[int]>> = rptr([1,2,3,4]);`
//               (tests/samples/const.fin:98) hands a fixed-size literal to a bare `T`,
//               and read the arguments first and T is `[int; fixed]`, which the
//               annotation one character to the left then rejects because a struct
//               compares its generic arguments exactly. Seeded from the hint, T is
//               `[int]` and the literal decays into it, which is the rule
//               `let a <[int]> = [1,2,3];` has always had.
//   the arguments   each written argument against the parameter it is passed to.
//
// The cost of the hint winning is where a real mismatch is reported: `let b
// <Box<string>> = Box(1);` says "expected 'string', got 'int'" at the argument rather
// than naming the two Boxes. That points at the mistake instead of at the call.
//
// `owner` is the type the callee was reached *through*, when there is one -- a static
// call's receiver. Its instantiation is what `Self` becomes, which a `<&Self>` return
// type and a `&Self` parameter both need (tests/samples/letssee.fin:73, :77). Null for a
// call reached by a bare name, where the return type carries the whole answer.
//
// One substitute() does both halves: FunctionType::substitute rebuilds the parameters
// *and* the return type, so the parameters the arguments are checked against and the type
// the call takes come out of the same instantiation and cannot disagree.
std::shared_ptr<Type> SemanticAnalyzer::checkGenericCall(ASTNode& node, const char* kind,
                                                        const std::string& name,
                                                        FunctionType& sig,
                                                        std::vector<std::unique_ptr<Expression>>& args,
                                                        const std::shared_ptr<StructType>& owner,
                                                        TypeMap seed) {
    TypeMap mapping = std::move(seed);
    if (auto hint = hintFor(node)) unifyGeneric(sig.return_type, hint, mapping);

    checkCallArity(node, kind, name, sig, args.size());

    std::vector<std::shared_ptr<Type>> argTypes;
    for (auto& arg : args) {
        arg->accept(*this);
        argTypes.push_back(lastExprType);
    }
    for (size_t i = 0; i < argTypes.size() && i < sig.param_types.size(); ++i) {
        unifyGeneric(sig.param_types[i], argTypes[i], mapping);
    }

    std::shared_ptr<Type> instantiatedOwner = nullptr;
    if (owner && mentionsGenericParam(owner)) {
        instantiatedOwner = owner->instantiate(orderedGenericArgs(owner, mapping));
    }
    auto isig = std::dynamic_pointer_cast<FunctionType>(sig.substitute(mapping, instantiatedOwner));
    if (!isig) return sig.return_type;

    for (size_t i = 0; i < argTypes.size() && i < isig->param_types.size(); ++i) {
        checkType(*args[i], argTypes[i], isig->param_types[i]);
    }
    return isig->return_type;
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
        auto ctor = st ? StructType::constructorFor(st) : nullptr;
        if (ctor) {
            funcType = std::dynamic_pointer_cast<FunctionType>(ctor);
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
                // constructorFor and not `constructors[0]`: a parent's is inherited,
                // rebound to construct this type rather than the parent
                // (Soundness_ConstructorInheritance). `struct CollectionError :
                // <Error> {}` gets `Error(msg: string)` this way, which four samples
                // call and none declare.
                if (auto ctor = StructType::constructorFor(st)) {
                    funcType = std::dynamic_pointer_cast<FunctionType>(ctor);
                } else {
                     // Nothing in the ancestry declares one, so the type is
                     // constructible with no arguments and with nothing else --
                     // Soundness_ConstructorInheritance
                     // .AStructWithNoConstructorAnywhereStillTakesNoArguments is what
                     // keeps `P(1)` an arity error rather than an unchecked call.
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

    // The generic arguments the call wrote for itself: `Box::<int>()`, and
    // `HashMap::<string, Data>()` at tests/samples/deeptest4.fin:11.
    //
    // FunctionCall::generic_args was set by the parser (the `Turbofish Call` production)
    // and read by nobody, so the one spelling that needs no inference at all was the one
    // that got none: the call was typed as the template and `a["Hi"].integer` on
    // deeptest4.fin:16 reported `Type 'U' is not a struct`.
    //
    // Paired positionally against the *return type's* parameters, which for a
    // constructor are the constructed struct's own, in declaration order. A free
    // function's turbofish binds nothing -- a FunctionType has parameter types and no
    // parameter names, so there is nothing to pair with; KnownDefect_Written-
    // GenericArguments.AFreeFunctionsTurbofishBindsNothing books it and
    // tests/samples/interfaces.fin:25 is the site.
    TypeMap written;
    if (!node.generic_args.empty()) {
        if (auto* retStruct = funcType->return_type ? funcType->return_type->as<StructType>() : nullptr) {
            if (node.generic_args.size() != retStruct->generic_args.size()) {
                // The same shape of typo an implements block's header can make, and the
                // same message: the call claims to be talking about this type's
                // parameters, so a different number of them is a mistake about the type.
                error(node, fmt::format("Generic count mismatch: '{}' declares {} parameter(s), "
                                        "the call writes {}",
                                        retStruct->name, retStruct->generic_args.size(),
                                        node.generic_args.size()));
            }
            const size_t n = std::min(node.generic_args.size(), retStruct->generic_args.size());
            for (size_t i = 0; i < n; ++i) {
                auto t = resolveTypeOrError(node.generic_args[i].get());
                // The sentinel is not bound: an undefined name in a turbofish is one
                // diagnostic where it is written, and binding it would print `<error>`
                // as a generic argument in every diagnostic about the result. Left
                // unbound, the parameter is inferred as if nothing had been written.
                if (t && !isErrorType(t)) written[retStruct->generic_args[i]->toString()] = t;
            }
        }
    }

    // A generic callee is instantiated from what is written to it.
    //
    // `let ta2 <rptr<int>> = rptr(5);` (tests/samples/const.fin:80) was typed as the
    // template -- `rptr<T>` -- and so reported a mismatch against the annotation three
    // characters to its left. The sample could not say what a smart pointer is for
    // without saying it wrong.
    //
    // The gate is the return type and not what kind of callee this is. A constructor was
    // the first callee whose return type is the thing being named at the call site, but
    // an enumerator is another (`Ok(10)`, tests/samples/enums.fin:44, bound by `extern
    // Result::Ok as Ok;` on 19) and a generic free function is a third
    // (Soundness_EnumInference.AGenericFreeFunctionInfersItsReturnFromItsArgument). What
    // they have in common is the whole rule: a return type that still mentions a
    // parameter has not been told which type it is, and the arguments and the hint are
    // what tell it. Every other call is unaffected -- it goes through
    // checkCallArguments in the order it always did.
    //
    // Which is why `as<StructType>()` is no longer part of the condition. It used to
    // sit in front of mentionsGenericParam and narrow the rule back down to the case
    // that motivated it, so `fun first<T>(a: [T]) <T>` was typed as `T`: an assignment
    // off it said `expected 'int', got 'T'` and a member read said `Type 'T' is not a
    // struct`, on programs with no mistake in them. A bare `T`, a `[T]` and a `&T` are
    // return types in exactly the same sense a `Box<T>` is, and unifyGeneric already
    // matched all three from the parameter side -- the gate was the only thing that
    // had not been told. Soundness_GenericReturn.
    if (funcType->return_type && mentionsGenericParam(funcType->return_type)) {
        lastExprType = checkGenericCall(node, "Function", funcName, *funcType, node.args,
                                        nullptr, std::move(written));
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

    // A call through the compiler API: `compiler.enums.resolve_id(value)`
    // (stdlib/enums.fin:13), `compiler.structs.select_field::<int>(...)`
    // (stdlib/types.fin:25), `compiler.components.gc.present()`. The whole of the
    // resolution -- which layer, whether the grant is there, arity and arguments --
    // is Analyzer_CompilerApi.cpp; this is the door.
    if (auto* api = dynamic_cast<const CompilerApiType*>(objType.get())) {
        lastExprType = resolveCompilerApi(node, *api, node.method_name,
                                         &node.args, &node.generic_args);
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
    // An enum against a primitive, in either direction. `operators.fin:26` writes
    // `cast<int>(s)` on a `<State>` and that sample is `//@ ok`, so this is the corpus's
    // requirement and not a convenience: an enum *is* an integer at the representation
    // level, and `enum State { Alive = 1, Dead }` writes the integer down.
    //
    // This arm restores something that used to work by accident. An enum's type was a
    // PrimitiveType until the enum became a real type, so the primitive-to-primitive arm
    // above admitted the cast along with every other pair; `operators.fin` went red the
    // hour the representation changed. What replaces the accident is narrower than the
    // accident was -- `is_enum` names the one struct-shaped type with an integer
    // reading, and every other struct is still refused, which is
    // Soundness_Enums.ACastFromAnEnumToAStructIsStillRefused.
    //
    // Symmetric, though only the one direction is in the corpus. `cast<State>(1)` is the
    // same fact read the other way: the enumerator's value is written in the
    // declaration, so an integer has a reading as an enum exactly as an enum has a
    // reading as an integer. What the symmetry does not admit is a source with no
    // integer reading at all -- arrays_enums.fin:23 carries `cast<Status>(arr)` as a
    // commented-out line marked "Should fail", and
    // Soundness_Enums.ACastFromAnArrayToAnEnumIsRefused is the guard that keeps it
    // failing now that an enum is a cast target.
    else if (isEnumType(sourceType) && dynamic_cast<const PrimitiveType*>(targetType.get())) valid = true;
    else if (dynamic_cast<const PrimitiveType*>(sourceType.get()) && isEnumType(targetType)) valid = true;
    
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
    
    // An array allocation resolves its *element* type rather than the whole
    // ArrayTypeNode, because the two paths disagree about the extent on purpose.
    // An annotation's extent has to be a constant -- `[int, n]` names a type whose
    // size nobody can state -- and an allocation's extent is exactly the run-time
    // value that rule exists to send here: `new [T, amount]{}` (collection.fin:54)
    // and `new [char, nbytes - self.pointer]` (stdio.fin:112). Handing this node to
    // resolveTypeFromAST would refuse both of the corpus's own allocations.
    //
    // The extent expression is still analysed, and against `int`, so that
    // `new [int, "x"]` and `new [int, nosuchvar]` are diagnosed here rather than
    // walked past.
    if (auto* arrNode = dynamic_cast<ArrayTypeNode*>(node.type.get())) {
        auto element = resolveTypeFromAST(arrNode->element_type.get());
        if (arrNode->size) {
            arrNode->size->accept(*this);
            if (lastExprType) {
                auto intType = currentScope->resolveType("int");
                if (!checkType(*arrNode->size, lastExprType, intType)) {
                    error(*arrNode->size, "An allocation's size must be an integer");
                }
            }
        }
        if (!element) { lastExprType = nullptr; return; }
        lastExprType = std::make_shared<ArrayType>(element);
        return;
    }

    auto allocatedType = resolveTypeFromAST(node.type.get());
    if (!allocatedType) {
        // A PointerType over a null pointee is worse than no type at all: it is
        // non-null, so every `if (!type) return;` downstream lets it through, and the
        // first toString() -- checkType's own error message, usually -- dereferences
        // the null. `new Nope{}` died in PointerType.cpp:6 that way. resolveTypeFromAST
        // has already reported why it failed, so there is nothing to add here.
        lastExprType = nullptr;
        return;
    }

    // Every other `new` is a pointer to what it allocated: `new Vec2::<float>{x:
    // 3.0, y: 4.0}` is a `&Vec2<float>` (tests/samples/letssee.fin:58). An array is
    // the exception, handled above, because `[T]` already carries a length and an
    // address -- there is nothing for the extra indirection to hold -- and the
    // corpus states it at every site:
    //
    //     self._arr = new [T, amount]{};   stdlib/collection.fin:54, `_arr` is `[T]`
    //     _arr: new [T, length]{}          :84, the same field through a literal
    //     self.stream_length = _temp.length;  stdlib/stdio.fin:130, a length off it
    //
    // Dynamic whatever the extent looks like, including when it is written as a
    // literal. `amount` is an `int` parameter and `nbytes + self.stream_length`
    // (stdio.fin:124) is an expression, so the count is not part of the type -- and
    // making it part of the type when the extent happens to be a literal would make
    // `new [int, 3]` and `new [int, n]` different types for no reason the language
    // draws anywhere else. An array *literal* is fixed instead: it states its
    // elements rather than an extent.
    lastExprType = std::make_shared<PointerType>(allocatedType);
}

void SemanticAnalyzer::visit(MemberAccess& node) {
    // `E::A`. The grammar builds this as a MemberAccess carrying `is_static` whose
    // object is an Identifier naming a *type* (parser.y, `IDENTIFIER DOUBLE_COLON
    // IDENTIFIER %prec STATIC_VALUE_PREC`), and until now nothing read the flag -- so
    // the walk below looked that type name up among the variables and reported
    // "Undefined variable 'Color'". enums.fin:19-20 and :35, literal_interface.fin:19,
    // extern_as.fin:44.
    //
    // Only the enum case is claimed here. The other production that sets `is_static`
    // puts a SuperExpression in `object`, which the cast excludes; and a name that
    // resolves to a type that is not an enum falls through to the walk below, whose
    // diagnostic is still the right one.
    if (node.is_static) {
        if (auto* id = dynamic_cast<Identifier*>(node.object.get())) {
            auto named = currentScope->resolveType(id->name);
            auto asStruct = std::dynamic_pointer_cast<StructType>(named);
            if (asStruct && asStruct->is_enum) {
                if (auto value = asStruct->getEnumeratorValueType(node.member)) {
                    // `E::A` read and not called: an E when the member has no payload,
                    // and the constructor when it has one -- the same rule the bare
                    // name follows, because `E::Ok` and `Ok` name the same member and
                    // differ only in how much of the path is written.
                    // StructType::getEnumeratorValueType carries it.
                    lastExprType = value;
                    return;
                }
                error(node, fmt::format("Enum '{}' has no member '{}'", asStruct->name, node.member));
                lastExprType = nullptr;
                return;
            }
        }
    }
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

    // The same door for a member that is read and not called: `compiler.enums.InBytes`
    // (stdlib/memory.fin:32), and every intermediate step of a longer path --
    // `compiler.types` and `compiler.components.gc` are both MemberAccess.
    if (auto* api = dynamic_cast<const CompilerApiType*>(objType.get())) {
        lastExprType = resolveCompilerApi(node, *api, node.member, nullptr, nullptr);
        return;
    }

    // `v.0` -- a member by position, which two kinds of type have and no other does.
    //
    // A prototype's positions are its two halves as arrays: "accessing
    // `{prototype}.0` returns an array of the keys" and "`.1` returns an array of the
    // values", stated in a comment on the line in stdlib/prototypes.fin:11 and :15 and
    // agreed with by the signatures around them -- `pub fun keys<T>(prtp: {T, any})
    // <[T]>` returns the `.0`. Dynamic arrays: how many entries a prototype holds is
    // not part of its type.
    //
    // An enum's positions are the slots of its members' payloads. `.N` is slot N of
    // whichever member the value holds, and not an index into a flattening of the
    // whole enum -- the corpus rules that out by writing the same `.0` for two
    // different members' payloads, under an `Ok` guard and in its `else`
    // (stdlib/typing.fin:30 and :32), and again with the members declared in the other
    // order (stdlib/stdio.fin:58 and :60, where `Err` comes first).
    //
    // Which member a value holds is not statically known, so a position the members
    // disagree at is an `any`: static erasure, which is what DynamicType.hpp says
    // `any` is. Where they agree there is nothing to erase and the slot keeps its
    // type, which is what makes `color1.0 == 100` (enums.fin:38) a real check --
    // `Color`'s two members are `uint{8}` at position 0 either way. Narrowing the
    // erased case by the guard the corpus writes needs `keyidof` of a payloaded member
    // to mean something first; KnownDefect_PositionalMembers
    // .ADisagreeingPayloadSlotIsNeverNarrowed books it.
    //
    // Anything else falls through, and the diagnostic it gets is still the right one:
    // a struct's fields are named, and reading `.0` as "the first field" is a ruling
    // with nothing in the corpus behind it.
    if (size_t position = 0; positionalMember(node.member, position)) {
        if (auto* proto = objType->as<PrototypeType>()) {
            if (position == 0 || position == 1) {
                lastExprType = std::make_shared<ArrayType>(
                    position == 0 ? proto->keyType : proto->valueType);
                return;
            }
            error(node, fmt::format("Type '{}' has no member '{}'", objType->toString(), node.member));
            lastExprType = nullptr;
            return;
        }
        // Through getStructType, so a positional read reaches an enum behind a pointer
        // exactly as far as a named read reaches a field behind one. Nothing in the
        // corpus needs it; the two forms differing would be the surprise.
        if (auto asEnum = getStructType(objType, currentScope); asEnum && asEnum->is_enum) {
            auto candidates = asEnum->enumeratorPayloadsAt(position);
            if (candidates.empty()) {
                error(node, fmt::format("Enum '{}' has no payload at position {}",
                                        asEnum->name, position));
                lastExprType = nullptr;
                return;
            }
            bool agree = true;
            for (const auto& c : candidates) {
                if (!c || !candidates[0] || !c->equals(*candidates[0])) { agree = false; break; }
            }
            if (agree) { lastExprType = candidates[0]; return; }
            auto anyType = currentScope->resolveType("any");
            lastExprType = anyType ? anyType : std::make_shared<DynamicType>("any");
            return;
        }
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
    // `is_enum` as well as the cast, and this is the first place the two-flags rule in
    // StructType.hpp bites: an enum's type became a StructType so that it could carry
    // methods, and the cast alone would therefore make `E{}` a legal instantiation of
    // one. It is not -- an enum is constructed by naming a member -- and the guard is
    // Soundness_Enums.AnEnumIsNotAStructEvenThoughItsTypeIsAStructType, which caught
    // this the hour the representation changed. An interface is refused for the same
    // reason and was already: nothing instantiates one either, and `is_interface` was
    // tested in the paths that mattered.
    if (!structDef || structDef->is_enum) {
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
    // The element type this literal is about to be checked against, when something
    // said one. `let b <[uint]> = [7, 3, 4];` (tests/samples/arrays.fin:29) reported
    // `expected '[uint]', got '[int, 3]'`: every element is a non-negative integer
    // constant, constantFitsType has said since the integer work that such a constant
    // is a `uint`, and the literal never asked -- it typed itself from its first
    // element and the whole array was then compared as a unit, by which point the
    // constants are gone and the only thing left to compare is `int` against `uint`.
    //
    // Offered to the elements, one at a time, exactly as checkCallArguments offers a
    // parameter type to an argument. The hint is keyed on the node, so this is the
    // *only* way it reaches inward -- and one level inward is all this does: an
    // element that is itself a literal gets the hint for its own elements from its own
    // annotation-shaped offer, not from this one.
    std::shared_ptr<Type> wanted = nullptr;
    if (auto hint = hintFor(node)) {
        if (auto* arr = hint->as<ArrayType>()) wanted = arr->element_type;
    }

    if (node.elements.empty()) {
        // `[]` has no element to infer from, so the annotation is the only thing that
        // can say what it holds -- tests/samples/prototype_test.fin:45 writes
        // `[5,5,5] : []` under `<{[int], [{int, string}]}>` and its own comment calls
        // it "empty array". Without one there is genuinely nothing, and the message
        // has to keep saying so rather than invent an element type.
        if (!wanted) {
            error(node, "Empty array literal cannot infer type.");
            lastExprType = nullptr;
            return;
        }
        lastExprType = std::make_shared<ArrayType>(wanted, uint64_t{0});
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
    typeHintFor = wanted ? node.elements[0].get() : nullptr;
    typeHint = wanted;
    node.elements[0]->accept(*this);
    typeHintFor = nullptr;
    typeHint = nullptr;
    auto firstType = lastExprType ? lastExprType : errorType();

    // What every element is compared against: the annotation's element type when there
    // is one, and otherwise the first element's, which is the rule an unannotated
    // literal has always had. The first element is compared too once there is a hint --
    // it is no longer the one that defines the answer, so it is no longer exempt from
    // it, and `let a <[string]> = [1, 2];` reports both of its elements rather than
    // only the second.
    auto expected = wanted ? wanted : firstType;

    // And, separately, the elements have to agree with *each other*. That is a
    // different question from whether each fits the annotation, and `[any]` is where
    // the two come apart: every type fits `any`, so asking only the annotation would
    // accept `let a <[any]> = [1, "x"];` -- and both corpus uses of `[any]` read it as
    // one unknown type rather than a mixed bag. stdlib/types.fin:102's
    // `resolve_arr_type(const &arr: [any])` answers for the whole array by reading
    // `arr[0]`, and stdlib/operators.fin:134 calls it "a static array of items with
    // unknown type", singular. The boxed spelling for genuinely mixed contents is
    // `object`, which prototype_test.fin:40 writes and :14's comment explains.
    //
    // Asked second, and only of an element that satisfied the annotation, so that one
    // mistake is still one diagnostic: `let a <[int]> = [1, "x"];` reports the element
    // against `int` and stops.
    //
    // And asked *only* of a dynamic element type, because everywhere else the
    // annotation is the authority on how much it pins down and asking again would
    // overrule it. `let a <[[uint]]> = [[1, 2], [3]];` is the case that shows it: the
    // annotation's element type is the dynamic `[uint]`, which waives the extent on
    // purpose, and comparing the second inner literal against the first would put the
    // extent back and report `expected '[uint, 2]', got '[uint, 1]'` about a program
    // that asked for neither. A DynamicType is the one target that constrains nothing,
    // which is exactly why it needs a second question asked and why nothing else does.
    const bool alsoCheckAgreement = wanted && wanted->as<DynamicType>() &&
                                    !typesEqual(expected, firstType);

    if (wanted) checkType(*node.elements[0], firstType, expected);

    for (size_t i = 1; i < node.elements.size(); ++i) {
        typeHintFor = wanted ? node.elements[i].get() : nullptr;
        typeHint = wanted;
        node.elements[i]->accept(*this);
        typeHintFor = nullptr;
        typeHint = nullptr;
        auto elemType = lastExprType;
        if (checkType(*node.elements[i], elemType, expected) && alsoCheckAgreement) {
            checkType(*node.elements[i], elemType, firstType);
        }
    }

    // The literal's own extent: it states its elements, so it knows how many. This
    // was `true` -- a flag meaning "fixed, count unknown" -- and the count was
    // sitting right here in `node.elements`, which is what made `let a <[int, 3]> =
    // [1, 2];` compile and every read of `a[2]` after it a word nobody wrote.
    //
    // And `expected` rather than `firstType` for the element type, which is what stops
    // the elements' diagnostics from being followed by the array's own. `let a <[uint]>
    // = [7, 3, 4]` is an `[int, 3]` under the old rule, and `[int]` does not fit
    // `[uint]` however well each constant does -- so the per-element check would have
    // been an addition rather than a replacement, and every accepted literal would have
    // reported anyway. Where the elements disagree with the hint they have already said
    // so, once each, at the element (AnUnrelatedAnnotationDoesNotBecomeTheElementType);
    // adopting the type they were checked against is what keeps that the whole report.
    lastExprType = std::make_shared<ArrayType>(expected, node.elements.size());
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
            defineParameter(*param, t);
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

    // 3. An enumerator, before a method. `enums.fin:35` writes `Color::RGB(100, 200,
    // 50)`, which the parser gives the same shape as a static call because it is the
    // same shape -- a type, `::`, a name, arguments -- and the enumerator's stored
    // FunctionType is a signature like any other, so the argument check below is the
    // one every other call gets rather than a second one written here.
    //
    // Before the method lookup and not after it, so that the enum's own members win a
    // name they share with a method. Nothing decides that ordering for us -- no sample
    // writes the collision -- but a member is written in the enum's own declaration and
    // a method arrives from an implements block somewhere else, and the more local
    // declaration winning is the rule everywhere else in the language.
    // Soundness_Enums.AStaticMethodOnAnEnumIsStillCallableThroughItsType is the guard
    // that this does not shadow the methods it is not about.
    if (structType->is_enum) {
        if (auto ctor = structType->getEnumerator(node.method_name)) {
            if (auto* sig = ctor->as<FunctionType>()) {
                // `Opt::Some(1)` and `Some(1)` are the same construction written two ways
                // -- `extern Result::Ok as Ok;` (enums.fin:19) exists to turn one into
                // the other -- so an enumerator reached through `::` is inferred exactly
                // as the bare name is, and by the same helper.
                if (mentionsGenericParam(structType)) {
                    lastExprType = checkGenericCall(node, "Enum member", node.method_name,
                                                    *sig, node.args, structType);
                    return;
                }
                checkCallArguments(node, "Enum member", node.method_name, *sig, node.args);
                lastExprType = sig->return_type;
            } else {
                for (auto& arg : node.args) arg->accept(*this);
                lastExprType = ctor;
            }
            return;
        }
        // Not falling through to the method lookup with an `Enum 'X' has no member 'Y'`
        // of its own: an enum reached here through `::` may still be calling a method
        // from its implements block, and `E::m(...)` must not be reported as a missing
        // member. The name that is neither gets the "Static method not found" below,
        // which is the truthful message for a `::` call on a type that has no such
        // callable -- visit(MemberAccess&) is where a *non-call* `E::Nope` is reported
        // as a missing member, because there the only reading left is an enumerator.
    }

    // 4. Look up Method
    
    auto methodType = structType->getMethodType(node.method_name);
    if (!methodType) {
        error(node, fmt::format("Static method '{}' not found in '{}'", node.method_name, structType->toString()));
        lastExprType = nullptr;
        return;
    }

    // 5. Analyze Args
    //
    // Against the same stored signature as an instance call, which is the signature as
    // *called*: the receiver is not in it. Calling an instance method through `::` and
    // passing the receiver by hand would therefore be one argument over -- no corpus
    // sample does it (the four `::` calls are prototype_test.fin:27, :30,
    // stdlib/stdio.fin:153 and nullifier.fin:26, all static), so which of the two
    // spellings that is stays an owner question rather than a rule invented here.
    if (auto* sig = methodType->as<FunctionType>()) {
        // A static call has no receiver, so nothing carries the generic arguments in.
        // `Vec2::normalize(scaled)` resolves `Vec2` by name and gets the template, whose
        // `Self` is still Self and whose `T` is still T -- so the signature reported
        // against every argument passed to it and every use of its result. That was
        // three of tests/samples/letssee.fin's three diagnostics.
        //
        // The arguments and the annotation are what say otherwise, so the same two
        // sources the constructor path reads are read here, and the signature is checked
        // and typed as the instantiation. `Self` is replaced too, by handing
        // substitute() the instantiated struct: a signature written `&Self` on the
        // template is `&Vec2<float>` on a `Vec2<float>`, which is exactly what a method
        // call already gets for free because its receiver was instantiated.
        //
        // Gated on the target still mentioning a parameter, so `Vec2::<float>::f(...)`
        // and every static call on a non-generic struct go through checkCallArguments
        // untouched, in the order they always did.
        //
        // The hint is read for the same reason: two of the three corpus sites --
        // `Vec2::from_angle(0.7854)` (letssee.fin:59) and `Vec2::zero()` (:77) -- have no
        // argument that mentions T at all, and the sample's own comment on 59 calls it
        // "inference on static call".
        if (mentionsGenericParam(structType)) {
            lastExprType = checkGenericCall(node, "Static method", node.method_name, *sig,
                                            node.args, structType);
            return;
        }

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
