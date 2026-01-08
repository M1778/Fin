#pragma once
#include "../nodes/ASTNode.hpp"
#include <vector>
#include <memory>

namespace fin {

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> e);
    void accept(Visitor& v) override;
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i);
    void accept(Visitor& v) override;
};

}
