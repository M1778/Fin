#pragma once
#include "Type.hpp"

namespace fin {

// `T?` -- tests/samples/nullifier.fin is the specification. A wrapper rather
// than a `bool` on Type, because Scope::resolveType("int") hands back one shared
// PrimitiveType for every `int` in the program: setting a flag on it would make
// every `int` in the module nullable. PointerType is the model -- same shape,
// same reason (one `int` wrapped many different ways).
//
// Nesting is deliberate. parser.y sets `is_nullable` on the outer TypeNode, so
// `p? <&int>` is `(&int)?` and `p <&int>` is `&int`; the two are distinct written
// types even though both are nullable at runtime, and collapsing them would make
// `?` mean "pointer" in one position and "maybe" in another.
class NullableType : public Type {
public:
    TypePtr inner;
    explicit NullableType(TypePtr t) : inner(std::move(t)) {}
    std::string toString() const override;
    bool equals(const Type& other) const override;
    bool isAssignableTo(const Type& other) const override;
    bool isCastableTo(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

// The type of the literal `null`, and nothing else.
//
// It used to be `&void` (Analyzer_Expr.cpp, KW_NULL), which bought pointer
// assignability for free and cost everything else: `let x <int> = null` reported
// "expected 'int', got '&void'", naming a pointer type in a diagnostic about a
// program containing no pointer. It also meant `null` and a genuine `&void`
// value were indistinguishable, so any rule written for one applied to the other.
//
// A distinct bottom type is assignable to every `T?` and to every `&T` -- a
// pointer is nullable in Fin (deeptest3.fin:80 `pub next <&Node> = null`) and
// stays that way -- and to nothing else. It is not a NullableType: there is no
// inner type to unwrap, `null?` is not a denullify of anything, and `T?` accepts
// it by a rule of its own rather than by the widening rule.
class NullType : public Type {
public:
    std::string toString() const override { return "null"; }
    bool equals(const Type& other) const override { return other.as<NullType>() != nullptr; }
    bool isAssignableTo(const Type& other) const override;
    bool isCastableTo(const Type& other) const override;
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override {
        return std::make_shared<NullType>();
    }
    TypePtr clone() const override { return std::make_shared<NullType>(); }
};

// True for the literal `null`'s own type. Named because three separate rules ask
// the question -- a declaration may be initialised to null against any type, a
// comparison against null is always legal, and a `T?` accepts it -- and each of
// them reads better as a question than as a dynamic_cast.
bool isNullLiteral(const TypePtr& t);

}
