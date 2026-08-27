#pragma once
#include <cstdint>
#include <optional>

#include "Type.hpp"

namespace fin {

class ArrayType : public Type {
public:
    TypePtr element_type;

    // How many elements, when the type says how many.
    //
    // Nullopt is `[T]` -- a dynamic array, whose length is a run-time value. A
    // number is `[T, 5]` and an array literal, both of which state their extent in
    // the source: the annotation writes it, the literal is its elements.
    //
    // This replaced a `bool is_fixed_size`, and the flag is why the layout pass
    // refused *every* array rather than only the dynamic ones: `[int, 4]` and
    // `[int, 8]` were one type, so any size the layout pass reported would have
    // been a guess, and a guessed size is a struct that is silently the wrong
    // shape. ArrayTypeNode carried the extent as an expression all along -- the
    // cloner and the macro expander both preserved it -- and resolveTypeFromAST
    // type-checked it against `int` and dropped the value on the floor.
    //
    // A run-time extent is deliberately not representable. `[int, n]` for a
    // variable `n` is refused in an annotation rather than stored as "fixed, size
    // unknown", because that state is the defect above in a new spelling: it has a
    // size that nobody can name. Fin's spelling for "however many at run time" is
    // `new [T, n]`, which is a dynamic `[T]`.
    std::optional<uint64_t> extent;

    // `[T]`.
    explicit ArrayType(TypePtr elem) : element_type(std::move(elem)) {}
    // `[T, n]`.
    ArrayType(TypePtr elem, uint64_t count)
        : element_type(std::move(elem)), extent(count) {}
    // Whatever the source said -- for clone() and substitute(), which carry an
    // extent through without needing to know whether there is one.
    ArrayType(TypePtr elem, std::optional<uint64_t> count)
        : element_type(std::move(elem)), extent(count) {}

    // The signature this class had until the extent was resolved, kept deleted
    // rather than removed. `ArrayType(elem, true)` would otherwise still compile:
    // `bool` converts to `std::optional<uint64_t>` and the array would come out
    // with exactly one element, which is a number, and a number is what a backend
    // and a collector both believe. Deleting it makes every old call site a
    // compile error that has to say which extent it means.
    ArrayType(TypePtr, bool) = delete;

    bool isFixed() const { return extent.has_value(); }

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
    bool isAssignableTo(const Type& other) const override;
};

}
