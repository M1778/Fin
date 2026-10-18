// SPDX-License-Identifier: GPL-3.0-or-later
#include "TypeImpl.hpp"
#include <iostream>

namespace fin {

// The registry is a leaked `new` on purpose (a destructor cannot run at exit
// safely: nodes may reference each other), so the sweep owns the teardown.
// See TypeSweep.hpp for the mechanism and the reason it is exit-time.
void typeGraphSweep() {
    std::vector<const Type*> pending(typeGraphNodes().begin(), typeGraphNodes().end());
    for (const Type* node : pending) {
        auto found = typeGraphNodes().find(node);
        if (found == typeGraphNodes().end()) continue;
        typeGraphNodes().erase(found);
        // Dropping the edges may free this very node by cascade; its destructor
        // unregistered it above, so nothing is read through `node` afterwards.
        const_cast<Type*>(node)->releaseTypeEdges();
    }
    typeGraphNodes().clear();
}

// --- Helper ---
bool typesEqual(const TypePtr& a, const TypePtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return a->equals(*b);
}

// A mutable container's question about what it holds. See Type.hpp for why it is not
// the same question as `isAssignableTo`.
//
// A narrowing of assignability and never a second opinion on it: the first line asks
// the ordinary question, and everything after it asks what the conversion *costs*. So
// no conversion becomes possible here that was impossible before, and the only thing
// this can do is refuse.
//
// The three containers ask it of their contents -- PointerType of its pointee,
// ArrayType of its element, PrototypeType of its key and value -- which is also what
// makes it recursive without restating any container's own rule. `&&int -> &&long` is
// refused two levels down, by the same line that refuses `&int -> &long`.
//
// NullableType deliberately does not ask. A `T?` is a value that gets copied, not an
// object that gets a second name, so `int? -> long?` is the value question and
// `isAssignableTo` is the right one to ask about it.
//
// What is permitted, and each entry is a target that stores what the source stored:
//
//   * The same type. Most of the corpus, and the reason this is cheap.
//   * A void pointer in either direction, at any depth -- answered by PointerType
//     before it consults its pointee, so it does not reach here. Every pointer is one
//     word, so `&&int -> &&void` puts a pointer where a pointer belongs.
//   * `any` and `object`, which the corpus demands through an array:
//     stdlib/types.fin:102 takes `const &arr: [any]`, and
//     Soundness_DynamicTypes.AnArrayOfAnyAcceptsAnArrayOfInt holds it. This one *is*
//     a representation change -- docs/plan.md fixes `any` as `{i8*, i64}` and an
//     `int` is four bytes -- so it is the one hole this function keeps open, on the
//     corpus's authority rather than on a rule. It cannot produce a wrong program
//     today: codegen refuses `[any]` outright (`a parameter of type '[any]' is not
//     lowered yet`), so the conversion has nowhere to be wrong yet.
//     KnownDefect_ContainerVariance.ADynamicElementTypeStillAcceptsAConcreteOne books
//     it.
//   * `auto`, which is an inference marker and not a storage type at all.
//     Soundness_DynamicTypes.TwoArraysWithAssignableElementsAreAssignable holds
//     `[int] -> [auto]`.
//   * A generic parameter, which is a name for a type that substitution has not
//     supplied yet. `fun f<T>(p: &T)` called as `f::<int>(&x)` lowers and returns 5.
//   * `Self`, which is a second name for one struct.
//
// What is refused, and both were measured wrong before this existed:
//
//   * An integer widening. `let x <int> = 1; let p <*long> = &x; *p = 4294967297;`
//     compiled clean and exited 139 -- an eight-byte store through a four-byte slot.
//     `&int -> &float` did not even crash, which is worse: it printed 1069547520.
//   * A struct to an interface it implements. ADR 0019 fixes an interface reference as
//     `{data, vtable}`, two words, and a `&Sq` is one. `f(p: &Shape) { return
//     p.area(); }` called with `&q` compiled clean, linked, and exited 139 on the
//     method call. No corpus file writes an interface in a pointee or an element
//     position, so nothing is lost by refusing it; the by-value conversion the corpus
//     does write (love.fin:38) is untouched.
bool isAssignableThrough(const TypePtr& from, const TypePtr& to) {
    if (!from || !to) return false;

    if (!from->isAssignableTo(*to)) return false;
    if (typesEqual(from, to)) return true;

    // A container pair answered for its contents on the way in -- the call above
    // reached this function again, one level down -- so there is nothing left to ask
    // about it here. Without these three lines the veto at the bottom would reject
    // every container whose contents are not identical, including the ones the
    // recursion just approved.
    if (from->as<PointerType>() && to->as<PointerType>()) return true;
    if (from->as<ArrayType>() && to->as<ArrayType>()) return true;
    if (from->as<PrototypeType>() && to->as<PrototypeType>()) return true;

    if (auto* self = from->as<SelfType>()) return isAssignableThrough(self->originalStruct, to);
    if (auto* self = to->as<SelfType>()) return isAssignableThrough(from, self->originalStruct);

    if (to->as<DynamicType>()) return true;
    if (to->as<GenericType>()) return true;
    if (to->toString() == "auto") return true;

    // Assignable, and it changes the bytes. That is the whole of what this function
    // exists to stop.
    return false;
}

// --- Base Type ---
bool Type::isAssignableTo(const Type& other) const {
    if (this->equals(other)) return true;
    if (other.toString() == "auto") return true;

    // Every type fits `any` and `object`. Here rather than as an override on
    // DynamicType because the rule is about the *target*: an override can only speak
    // for the source, and it is every other type that has to accept this target.
    //
    // Here rather than in checkType, too, and that is the load-bearing choice: this
    // is reached recursively -- ArrayType::isAssignableTo asks its element types, so
    // `[int]` fits `[any]` through this line. A copy in checkType would answer for
    // `any` and not for `[any]`, and stdlib/types.fin:102 takes `const &arr: [any]`.
    //
    // NullType is excluded, and the corpus is explicit about why. nullifier.fin:36
    // writes `let _ <any> = mibombo?;` and comments "this should be an error since
    // type any cannot be null". `null` is not a value of some type that is being
    // erased; it is the absence of one, and NullType::isAssignableTo falls through to
    // here -- so a rule stated as "a dynamic target accepts anything" accepts null
    // too, silently. A program that means it writes `any?`, which is a NullableType
    // and is answered before this. (Ruling the other side of that line is case 2 of
    // issue #96 and is still open: nothing here closes it.)
    if (auto* otherDyn = other.as<DynamicType>()) {
        if (this->as<NullType>()) {
            // Refused as before; the fall-through reaches the bottom.
        } else if (auto* thisDyn = this->as<DynamicType>()) {
            // Issue #96 (R5, one-way): `any -> object` is the box's known-content
            // spelling and stays; `object -> any` is refused -- an `object` carries
            // its type at runtime and an erased slot cannot say what to erase, which
            // is the distinction corpus evidence draws (stdlib/types.fin:97 -- "any
            // type that is visible in compile time" -- against prototype_test.fin:40
            // -- "an expensive type ... at the cost of memory and speed"). The other
            // direction through an explicit `cast` stays open; the help text points
            // there from checkType.
            if (thisDyn->name == "object" && otherDyn->name == "any") return false;
            return true;
        } else if (satisfiesBound(this, otherDyn)) {
            // The bound consulted (R1, #96): an alias spelling a bound
            // (`type Bound = any implements <I>`, or `getkeyid(value: EnumType)`)
            // accepts a value only when the value is an implementor of the bound's
            // interface, or the interface is memberless. Without this the bound was
            // recorded on the DynamicType and never read by anyone.
            return true;
        }
        // Either null into a dynamic type, or a bound no value satisfies: refused.
    }

    // No rule for the error sentinel here, deliberately. checkType short-circuits on
    // isErrorType before it ever calls this (Analyzer_Core.cpp:361), and checkType is
    // the only caller outside this directory -- so a rule here could not fire, and a
    // mutation matrix proved it: removing this line, removing ErrorType's own
    // override, and removing the short-circuit each killed nothing, because any two
    // covered for the third. One mechanism, in the one place that also suppresses the
    // message. If a second caller of isAssignableTo ever appears it must short-circuit
    // the same way rather than reinstate this.

    // A `T?` slot accepts a plain `T`. nullifier.fin:7 returns an `int?` member
    // from a body whose declared return type is `int` under `fun?`, and :27
    // assigns an `A?` call result into an `A?` binding.
    //
    // The two sources this must not answer for are a NullableType (`int?` into
    // `string?` is a question about the inner types, not about both sides being
    // nullable) and NullType (`null` fits *every* `T?`, whatever the inner type
    // is). Both have their own overrides, and both call back into here, so
    // stepping over them is also what stops the recursion.
    if (auto* otherNullable = other.as<NullableType>()) {
        if (!this->as<NullableType>() && !this->as<NullType>())
            return this->isAssignableTo(*otherNullable->inner);
    }

    if (auto* self = this->as<SelfType>()) {
      return self->originalStruct->isAssignableTo(other);
    }
    if (auto* otherSelf = other.as<SelfType>()){
      return this->isAssignableTo(*otherSelf->originalStruct);
    }
    
    // The array-decay and void-pointer rules used to be repeated here, and both copies
    // were unreachable in practice. Each subclass override opens with
    // `if (Type::isAssignableTo(other)) return true;`, so the base copy only ever runs
    // from *inside* the override that also states the rule -- and the base copies were
    // the stricter of the two pairs (`element_type->equals` vs `isAssignableTo`;
    // decay-only vs both directions). A strictly narrower rule that answers first can
    // never change an answer.
    //
    // Dominated code is not free. A mutation matrix on the array rule killed nothing:
    // breaking ArrayType's copy left the base's copy answering, and breaking the base's
    // copy left ArrayType's. Two arms, one rule, no test able to see either. Deleting
    // the duplicates makes the surviving rule load-bearing, which is the only state in
    // which a test can hold it.
    //
    // Soundness_Arrays.AFixedListInitialisesADynamicArrayOfTheSameElementType and
    // Soundness_Pointers.AVoidPointerIsAssignableInBothDirections cover what is left.

    if (dynamic_cast<const GenericType*>(&other)) return true;

    // A union alias as a target: `let x <Number> = 5` stores into the first
    // alternative, which is what the alias resolved to before unions were
    // types (UnionType.hpp). Kept exact so that preserving the union changes
    // no value that stores today; alternative-wise rules are a separate ruling.
    if (auto* target = dynamic_cast<const UnionType*>(&other)) {
        if (target->alternatives.empty() || !target->alternatives[0]) return false;
        return this->isAssignableTo(*target->alternatives[0]);
    }

    // A struct converts to an interface it implements (owner ruling, 2026-08-28).
    //
    // `tests/samples/love.fin` is the corpus's first and only witness: `I.love(F)` at
    // :38 hands a `Fin` to a parameter declared `<Person>`, both structs declare
    // `: <Person, ...>`, and both carry the `name <string>` the interface requires. ADR
    // 0019 fixed the representation of an interface reference -- `{data, vtable}`, two
    // words -- while recording that "interface-as-a-runtime-type does not exist in the
    // corpus"; that sample supplies what the ADR was written without.
    //
    // Guarded on the *target* being an interface and the source being a struct that is
    // not one, so this cannot make two unrelated structs assignable, and cannot make an
    // interface assignable to a struct -- an interface reference carries no fields of
    // its own to satisfy a struct's, and the corpus asks for the conversion in one
    // direction only.
    //
    // `implements()` is the same predicate the declaration-site conformance check uses
    // (`X does not implement Y`), which is what makes an accepted conversion and an
    // accepted declaration agree by construction rather than by two rules that happen
    // to match. Its known gap is fields: it walks methods, operators, constructors and
    // the destructor and never fields
    // (KnownDefect_Interfaces.AMissingFieldIsAccepted). So a struct missing a required
    // *field* converts today and should not; that is the existing defect's to fix, and
    // narrowing it here would put the rule in two places.
    //
    // The backend is where this becomes real: ADR 0027 has the `{data, vtable}` layout
    // and the field-offset slots. Until a call site is lowered, an accepted conversion
    // is a codegen refusal rather than a wrong program.
    if (auto* target = dynamic_cast<const StructType*>(&other)) {
        if (target->is_interface) {
            if (auto* src = dynamic_cast<const StructType*>(this)) {
                if (!src->is_interface && src->implements(target)) return true;
            }
        }
    }

    return false;
}

// An interface's member set: the only thing an implements bound is able to demand
// when it reads as vacuous. Kept beside isAssignableTo rather than on StructType so
// that the question "is this bound checkable" and the question "does it hold" live in
// one file; `implements()` itself is not moved, and the declaration-site check keeps
// its own call.
static bool memberSetEmpty(const StructType& s) {
    return s.fields.empty() && s.methods.empty() && s.operators.empty() &&
           s.constructors.empty() && s.enumerators.empty() && !s.has_destructor;
}

bool satisfiesBound(const Type* value, const Type* bound) {
    // A bound that failed to resolve is already reported where it was written; a
    // missing value is the sentinel the analyser carries instead of a type. Neither
    // adds a second diagnostic.
    if (!bound || !value) return true;

    // A generic value is a type nobody has substituted yet, and the substitution is
    // checked at the moment it becomes literal. Refusing the spelling the generic body
    // writes would refuse the body itself.
    if (value->as<GenericType>()) return true;

    // A dynamic bound: `type Bound = any implements <I>` or `T: any implements <I>`.
    // Elision records nothing, and every non-elision bound must hold in turn.
    if (auto* dyn = bound->as<DynamicType>()) {
        if (dyn->name == "...") return true;
        // `Struct` is its own interface-like bound kind, ruled in
        // Soundness_DynamicTypes.AStructTargetKeepsItsImplementsBound: member lookup
        // goes through it to the struct, and the corpus writes it where the value must
        // be a struct (`make_default<T: any implements Struct>`, literal_struct.fin).
        if (dyn->name == "Struct" && !value->as<StructType>()) return false;
        for (const auto& b : dyn->bounds)
            if (!satisfiesBound(value, b.get())) return false;
        return true;
    }

    // The interface case, and the only one that can refuse. "X's type is an
    // implementor of I": the same predicate the declaration-site check runs, which is
    // what makes an accepted instantiation and an accepted `: <I>` one rule.
    if (auto* iface = bound->as<StructType>()) {
        if (!iface->is_interface) {
            // Not an interface: a class (`<Error>`) or one of the builtin mocks
            // (`Castable`, the erasure marker). A member set that is empty satisfies
            // vacuously (R1's own words); a member set that is not is an
            // inheritance-shaped reliance the analyzer does not answer for --
            // `pub struct HashMapError : <Error> {}` is shipped library code whose
            // conformance against `Error`'s fields is not what Structural compares.
            return memberSetEmpty(*iface);
        }
        if (memberSetEmpty(*iface)) return true;  // the `Enum` case; vacuous per R1
        auto* v = value->as<StructType>();
        return !!v && v->implements(iface);
    }

    // Everything else on the bound side has no member set to compare and is
    // unchecked today by corpus rule: a class bound above, a union constraint
    // (`E: Offer<string, Error>`, whose own lib comment pins it unchecked), a
    // primitive classifier bound (`T: Number`), and a pointer bound
    // (`any implements <&void>`, stdlib/types.fin:78). None of them is what the
    // ruling answered, and tightening one would be its own ruling.

    return true;
}

}
