#pragma once

#include "../nodes/ASTNode.hpp"
#include <string>
#include <vector>
#include <memory>

namespace fin {

class GenericParam;

class TypeNode : public ASTNode {
public:
    std::string name; 
    std::vector<std::unique_ptr<TypeNode>> generics;
    std::vector<std::unique_ptr<Expression>> annotations; // New field
    bool is_prototype = false; // New field
    int pointer_depth = 0; 
    bool is_array = false;
    // `?` on a declarator marks the declared type nullable: `b? <int>` is a
    // member whose default is null, `fun? f() <int>` returns int-or-null,
    // `n?: int` is a parameter that may be null, and `let x? <A>` is a variable
    // that may be null (tests/samples/nullifier.fin, undefined_behavior.fin).
    // The flag lives on the type rather than on each of the four declaration
    // nodes because nullability is a property of the type in all four -- the
    // corpus spells the member case out as "equavelant to `b <int> = null`" and
    // the function case as "equavelant to <Maybe<A>>".
    bool is_nullable = false;
    // `const a: int` on a parameter (tests/samples/const.fin:11). On the type
    // rather than on Parameter for the same reason is_nullable is: the four
    // declarators that can be const would otherwise need four copies of the
    // flag, and the pass that already refuses assignment to an immutable local
    // is looking at the type when it decides.
    bool is_const = false;
    // `any implements <Error>` as one alternative of a union type alias
    // (tests/samples/stdlib/typing.fin:10). On the type and not on
    // TypeDefinition because it constrains that one alternative: the alias
    // there is `string | any implements <Error>`, and the constraint set says
    // nothing about `string`.
    std::vector<std::unique_ptr<TypeNode>> implements_list;
    std::unique_ptr<Expression> array_size = nullptr;
    TypeNode(std::string n);
    void accept(Visitor& v) override;
};

class FunctionTypeNode : public TypeNode {
public:
    std::vector<std::unique_ptr<TypeNode>> param_types;
    std::unique_ptr<TypeNode> return_type;
    // The names of the parameters, index-parallel with `param_types` and empty
    // where the type was written without them: `fn<T: Castable>(m: T) -> T`
    // (tests/samples/lambdas.fin:69) names its parameter, every other fn type in
    // the corpus does not.
    std::vector<std::string> param_names;
    // `fn<T: Castable>(...)`: the generic parameters of the function type itself.
    // GenericParam is only forward-declared here because GenericParam.hpp includes
    // this header (a GenericParam holds a TypeNode constraint), so the destructor is
    // declared here and defined in TypeNode.cpp where the type is complete.
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret);
    ~FunctionTypeNode() override;
    void accept(Visitor& v) override;
};

class PointerTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> pointee;
    PointerTypeNode(std::unique_ptr<TypeNode> p);
    void accept(Visitor& v) override;
};

class ArrayTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> element_type;
    std::unique_ptr<Expression> size; // Optional
    ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s = nullptr);
    void accept(Visitor& v) override;
};

} // namespace fin
