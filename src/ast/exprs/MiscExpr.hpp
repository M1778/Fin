#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class CastExpression : public Expression {
public:
    std::unique_ptr<TypeNode> target_type;
    std::unique_ptr<Expression> expr;
    CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class SizeofExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type_target;
    std::unique_ptr<Expression> expr_target;
    SizeofExpression(std::unique_ptr<TypeNode> t);
    SizeofExpression(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class TernaryOp : public Expression {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> true_expr;
    std::unique_ptr<Expression> false_expr;
    TernaryOp(std::unique_ptr<Expression> c, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f);
    void accept(Visitor& v) override;
};

class SuperExpression : public Expression {
public:
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    std::string parent_name; 
    std::vector<std::unique_ptr<Expression>> args;
    
    // Case 1: super { ... }
    SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    // Case 2: super::Parent(...)
    SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a);

    // Case 3: super::Parent { ... }
    SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    void accept(Visitor& v) override;
};

class MacroInvocation : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

}
