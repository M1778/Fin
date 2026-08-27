#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../stmts/Statement.hpp" // For Block
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {
class Expression;

class FunctionDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public;
    bool is_static = false;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;

    FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class OperatorDeclaration : public Statement {
public:
    ASTTokenKind op;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public;
    std::vector<std::unique_ptr<GenericParam>> generic_params;

    std::unique_ptr<TypeNode> implements_type;
    // `operator[] implements cast<fn(Self, T)>(__get)` --
    // tests/samples/stdlib/hashmap.fin:50. The operator is implemented by an
    // expression rather than by a body or by a type: the cast holds both the
    // signature and the function bound to the operator. Set instead of
    // `implements_type`, never alongside it.
    std::unique_ptr<Expression> implements_expr;

    OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, 
                        std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub);
    void accept(Visitor& v) override;
};

// One entry of a constructor's member-initialiser list: `age(10)` in
// `Person():age(10), name("Hello")` (tests/samples/deeptest2.fin:40). The
// arguments are a list because a member can be built from more than one.
struct MemberInit {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
};

class ConstructorDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<Block> body;
    std::unique_ptr<TypeNode> return_type;
    // `Person():age(10), name("Hello") { ... }` -- tests/samples/deeptest2.fin:40.
    // Empty for a constructor written without a member-initialiser list.
    std::vector<MemberInit> member_inits;
    
    ConstructorDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<Block> b, std::unique_ptr<TypeNode> rt = nullptr);
    void accept(Visitor& v) override;
};

class DestructorDeclaration : public Statement {
public:
    std::string name;
    std::unique_ptr<Block> body;
    
    DestructorDeclaration(std::string n, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
