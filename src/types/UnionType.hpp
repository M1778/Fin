#pragma once
#include "Type.hpp"
#include <vector>

namespace fin {

// A union type alias: `type Number = int | uint | float;`
// (tests/samples/arrays.fin:9, stdlib/types.fin:53, stdlib/typing.fin:10).
//
// Before this class the alias defined its first alternative and dropped the
// rest, so every downstream consumer answered for `int` while the program
// meant "one of these". Value positions keep that answer by staging (see
// `isAssignableTo`): what changes is that the union-ness survives resolution,
// so a consumer that cannot answer for a union -- the layout pass, whose
// pointer map of `int` would be a confident zero for a value that may hold a
// `float` -- refuses with a diagnostic instead (docs/compiler-api.md Q12).
//
// The corpus uses unions only as generic bounds (`fun sort<T: Number>`), which
// never consult this class: a non-struct constraint is unchecked, exactly as
// the `int` it used to resolve to was.
class UnionType : public Type {
public:
    // The alias as written, for diagnostics (`Number` rather than the expansion).
    std::string alias;
    // Every alternative in written order; `alternatives[0]` is the alias's
    // `aliased_type`, the rest its `union_members`.
    std::vector<TypePtr> alternatives;
    // The alias's generic parameters when it is a template (`Offer<T, E>`,
    // ADR 0046): the GenericTypes `declareGenericParams` collected at
    // declaration, exactly as StructType::generic_args. Empty for a concrete
    // union alias (`Number`). A use with concrete arguments instantiates by
    // substitution (instantiate, below); the template itself is never a value
    // type, which is what makes the alias an erasure.
    std::vector<TypePtr> generic_args;

    UnionType(std::string name, std::vector<TypePtr> alts)
        : alias(std::move(name)), alternatives(std::move(alts)) {}

    std::string toString() const override;
    bool equals(const Type& other) const override;

    // Staged assignability: both directions delegate to the first alternative,
    // which is bit-identical to the alias meaning before unions were types.
    // Alternative-wise rules (accept a member, demand every member) are the
    // next step and need their own ruling; guessing one here would change which
    // values store into union-typed slots, which Q12 does not ask to change.
    bool isAssignableTo(const Type& other) const override;

    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;

    // `Offer<string, Error>` from `Offer<T, E>`: the StructType::instantiate
    // rule, with the same arity contract (a mismatch is null, and the caller
    // reports `Generic count mismatch`).
    TypePtr instantiate(const std::vector<TypePtr>& concreteArgs);
};

} // namespace fin
