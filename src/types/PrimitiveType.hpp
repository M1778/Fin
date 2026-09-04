#pragma once
#include "Type.hpp"

namespace fin {

class PrimitiveType : public Type {
public:
    std::string name;

    // The width the source wrote inside the type, and 0 for "none written".
    //
    // `int{64}` is a written width and it *is* the type (ADR 0022), so the bits
    // live on the type rather than beside it. Before this field the annotation was
    // walked for its own diagnostics in resolveTypeUnwrapped and then dropped, so
    // `int{8}` and `int` were one semantic type -- which is why a narrowing
    // assignment could not be refused (there was nothing narrower to refuse), why
    // the layout pass answered four bytes for a one-byte field, and why a
    // diagnostic reading `expected 'uint'` was shown to someone who had written
    // `uint{8}`. All three are the same missing number, and this is where it goes.
    //
    // Not encoded into `name`, and that is the load-bearing half of the decision.
    // Seven sites compare a type's spelling against a bare name -- `toString() ==
    // "auto"` in Type.cpp twice, `"void"` in PointerType.cpp twice,
    // Analyzer_Decl.cpp:41, PrimitiveType.cpp's `int` -> `float` rule -- and two
    // more use a spelling as a substitution key (StructType.cpp:213,
    // Analyzer_Expr.cpp:1258). A name of `int{64}` would silently fail every one
    // of them, so `name` stays what the base name was and `toString()` is what
    // grows.
    //
    // 0 rather than std::optional because a zero-bit integer is not a thing the
    // grammar can hand over: `int{0}` is a front-end diagnostic ("A bit width
    // cannot be zero"), so the sentinel cannot collide with a real width. What a
    // width means for a name that is not an integer -- `float{128}`, `bool{1}` --
    // is nothing: the front end drops it and `scalarOf` drops it again, so the two
    // agree without either trusting the other.
    unsigned bits = 0;

    PrimitiveType(std::string n) : name(std::move(n)) {}
    // `int{64}`. The width the source wrote, whether or not it is one the name
    // already means: `int{32}` is `int` and says so through equals(), not by
    // being normalised away on the way in, so a diagnostic can name the spelling
    // the program used.
    PrimitiveType(std::string n, unsigned width) : name(std::move(n)), bits(width) {}

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override {
        return std::make_shared<PrimitiveType>(name, bits);
    }
    TypePtr clone() const override { return std::make_shared<PrimitiveType>(name, bits); }
    bool isAssignableTo(const Type& other) const override;
};

}
