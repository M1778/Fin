#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <memory>

namespace fin {

class Expression;
class Attribute;

class VariableDeclaration : public Statement {
public:
    bool is_mutable;
    bool is_public = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> initializer;
    std::vector<std::unique_ptr<Attribute>> attributes;
    // fin-guard: the parser accepted `let <name> = ...` with no annotation and
    // spelled the type as `<auto>`; semantics warns (or errors when the guard
    // is off) on this flag rather than on the type, so an explicit
    // `let <name> <auto> = ...` stays silent.
    bool finGuardRewritten = false;

    VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init);
    void accept(Visitor& v) override;
};

class DeleteStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    DeleteStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

}
