// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <iostream>

#include "TypeSweep.hpp"

namespace fin {

class Type;
using TypePtr = std::shared_ptr<Type>;
using TypeMap = std::unordered_map<std::string, TypePtr>;

class Type {
public:
    // Issue #101 (see TypeSweep.hpp): every node joins the exit-time graph sweep on
    // construction and leaves it in its destructor, so the registry outlives no
    // node and holds no node alive.
    Type() { typeGraphRegister(this); }
    virtual ~Type() { typeGraphUnregister(this); }
    virtual std::string toString() const = 0;

    // Equality (Strict)
    virtual bool equals(const Type& other) const = 0;

    // Compatibility (Assignment)
    virtual bool isAssignableTo(const Type& other) const;

    // Explicit casting with virtual method
    virtual bool isCastableTo(const Type& other) const {
        return this->equals(other);
    }

    // Generics Substitution
    virtual TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) = 0;

    // Cloning
    virtual TypePtr clone() const = 0;

    // Issue #101 (see TypeSweep.hpp): the process-exit sweep asks each surviving
    // node to drop the type-valued edges it owns. Edge-free nodes inherit this
    // empty default; a class that owns edges must override it or its shares of
    // the leak come back.
    virtual void releaseTypeEdges() {}

    template <typename T>
    const T* as() const { return dynamic_cast<const T*>(this); }
    
    template <typename T>
    T* as() { return dynamic_cast<T*>(this); }
};

bool typesEqual(const TypePtr& a, const TypePtr& b);

// Owner ruling, issue #96 (R1, structural conformance): a bound `X implements <I>`
// is satisfied by `X` iff X's type is an implementor of I, or I has an empty member
// set. The memberless set covers the builtin mocks (`Castable`, the erasure marker
// registered in Analyzer_Core.cpp) and the stdlib's `Enum` interface, which is what
// keeps `getkeyid(value: EnumType)` working: an enum value needs no member of its
// own to sit where the bound was written. A struct satisfies a member-full
// interface the same way the declaration-site check enforces it
// (`StructType::implements`), so an accepted conversion and an accepted declaration
// agree by construction.
//
// Bound kinds this deliberately does not consult, with the corpus evidence for
// each:
//   * elision (`<...>`, DynamicType ".."), which records nothing;
//   * `Struct`, whose own bound kind is ruled in
//     Soundness_DynamicTypes.AStructTargetKeepsItsImplementsBound -- satisfied by
//     any struct-shaped value;
//   * a class bound (`<Error>`), where the corpus's own rely is inheritance-shaped
//     (`pub struct HashMapError : <Error> {}` with no members of its own, lib/std
//     /hashmap.fin:79) and structural conformance is not what the analyzer answers
//     for it;
//   * primitive (classifier) bounds (`T: Number`), union bounds (`E: Offer`),
//     pointer bounds (`any implements <&void>`, stdlib/types.fin:78) -- none
//     declares a member set, and lib/std/types.fin's header pins every one of them
//     "unchecked as a bound the way every non-struct constraint is";
//   * an unresolved bound, which has already been reported where it was written.
//
// Returns false only where there is something to refuse: an interface with members
// against a value that is not its implementor.
bool satisfiesBound(const Type* value, const Type* bound);

// The question a *mutable container* asks about its element -- a pointee, an array
// element, a prototype's key or value. Not the same question as `isAssignableTo`,
// and the difference is the point: an assignment copies a value and may convert it
// on the way, while a container conversion keeps one object and hands out a second
// name for it. A conversion that changes a value's representation is sound in the
// first case and is a lie in the second.
//
// `&int -> &long` is that lie. Both are integers, `long` is wider, so ADR 0022's
// widening rule says yes -- and the object is still four bytes, so a store through
// the `&long` writes eight. It compiled clean and segfaulted.
bool isAssignableThrough(const TypePtr& from, const TypePtr& to);

} // namespace fin
