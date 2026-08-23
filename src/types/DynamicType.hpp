#pragma once
#include "Type.hpp"
#include <vector>

namespace fin {

// `any` and `object`: the two types that accept a value of any other type.
//
// Neither is declared anywhere in the corpus and both are used there, in files with
// no imports at all, which is what makes them builtins rather than library names --
// `nullifier.fin:34` writes `let mibombo? <any> = null;` and
// `prototype_test.fin:40` writes `<{object, object}>`. `Any` is the opposite case
// and is *not* here: `stdlib/types.fin:69` declares `pub type Any = any;` behind
// `#[export]`, so it is a library alias of the builtin and a program must import it.
//
// Two names in one class, because the analyser cannot yet tell them apart and
// pretending otherwise would be inventing semantics. The corpus does distinguish
// them, and says how:
//
//   `any`    -- "any type that is visible in compile time (can be identified in
//               compile time)" (stdlib/types.fin:97). A static erasure: the concrete
//               type is known to the compiler and forgotten in the signature.
//   `object` -- "an expensive type but can fit any datatype in it at the cost of
//               memory and speed" (prototype_test.fin:40). A box: the concrete type
//               is *not* known and is carried at runtime.
//
// That difference is a code-generation difference -- one is free, one allocates --
// so it becomes real when there is a code generator. The names are separate now so
// that the day it matters, every corpus file already says which it meant. This is
// the same reasoning that registered the `$type` family as four distinct names
// rather than a `$`-prefix rule (Analyzer_Core.cpp): registering a name is not
// deciding its semantics, and a name registered wrong is harder to find later than
// a name not registered at all.
//
// Not a PrimitiveType, deliberately. `Analyzer_Core.cpp` reads a primitive's *name*
// to decide whether an integer literal fits it, and its comment already worries
// about a new primitive being mistaken for a number; a separate class cannot be.
class DynamicType : public Type {
public:
    std::string name;   // "any" or "object"

    // The `implements` bound, when one was written: `type EnumType = any implements
    // <Enum>;` (stdlib/enums.fin:6), `type nullptr = any implements <&void>;`
    // (stdlib/types.fin:78). Empty for a bare `any`.
    //
    // Recorded and not yet enforced -- KnownDefect_DynamicTypes
    // .AnImplementsBoundOnADynamicTypeIsNotEnforced. It is stored rather than
    // dropped because what stood here fabricated a `StructType("any")` instead, and
    // a wrong answer that renders as a real type name is worse than a missing one:
    // `let x <EnumType> = 5;` reported `expected 'any', got 'int'`, naming a type
    // the program never wrote.
    std::vector<TypePtr> bounds;

    explicit DynamicType(std::string n) : name(std::move(n)) {}
    DynamicType(std::string n, std::vector<TypePtr> b)
        : name(std::move(n)), bounds(std::move(b)) {}

    std::string toString() const override;

    // Name and bounds both, so `any` and `object` are not silently the same type and
    // an `any implements <Enum>` is not an `any implements <Error>`. A bare `any` is
    // equal to a bare `any`, which is what makes `fun f(v: any)` callable with the
    // result of another `fun g() <any>`.
    bool equals(const Type& other) const override;

    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

// True when a type is `any` or `object`, unwrapped. Deliberately *not* recursive
// through Pointer/Array/Nullable the way `isErrorType` is: the sentinel has to be
// absorbed wherever it is buried, because a diagnostic about it would be a
// propagation bug, whereas `[any]` is an ordinary array type that a program means to
// write and whose assignability is answered element by element by ArrayType.
bool isDynamicType(const TypePtr& t);

} // namespace fin
