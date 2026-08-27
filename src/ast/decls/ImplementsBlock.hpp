#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include "FunctionDecl.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class ImplementsBlock : public Statement {
public:
    std::string target_type;
    // The target's own generic arguments: `Result<T, U> implements <IResult>`
    // (tests/samples/stdlib/typing.fin:27, stdlib/stdio.fin:54). The methods in
    // the body use these names, so they cannot be dropped.
    std::vector<std::unique_ptr<TypeNode>> target_generics;
    std::unique_ptr<TypeNode> interface_type;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    // A constructor written in an implements body:
    // `Collection<T> implements <NoLengthCollection> { Collection() { ... } }`
    // (tests/samples/stdlib/collection.fin:104), which is how that block satisfies
    // the `Self();` the interface declares. It is not a FunctionDeclaration, so it
    // cannot go in `methods`.
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    // Parse-time only: the default visibility for members of this body -- public
    // until a `pub:` / `priv:` label changes it (tests/samples/stdlib/stdio.fin:55).
    // On the accumulator so each body has its own, like
    // StructDeclaration::label_public, whose comment carries the reasoning.
    bool label_public = true;
    // `@implements Collection<T> { ... }` rather than
    // `Collection<T> implements <ICollection> { ... }`: the body's methods are
    // added to the target or overwrite its own, and no interface is named
    // (tests/samples/stdlib/collection.fin:93).
    bool is_overwriter = false;
    // The single-member form: `@implements Result<T, E>::unwrap = fun(...) { ... }`
    // (tests/samples/enums.fin:25) and `@implements(pub) Collection<T>::push_back =
    // (...) <noret> => {}` (tests/samples/stdlib/collection.fin:97). One named
    // member is added or replaced and the replacement is an expression -- a lambda
    // at both corpus sites -- so it cannot live in `methods`, which holds
    // declarations. An empty name means this is not that form.
    std::string overwrite_member;
    std::unique_ptr<Expression> overwrite_value;
    bool overwrite_public = false;
    
    ImplementsBlock(std::string target, std::unique_ptr<TypeNode> iface)
        : target_type(std::move(target)), interface_type(std::move(iface)) {}
    
    void accept(Visitor& v) override;
};

}
