#pragma once
#include "../nodes/ASTNode.hpp"
#include <memory>

namespace fin {

class BinaryOp : public Expression {
public:
    std::unique_ptr<Expression> left;
    ASTTokenKind op;
    std::unique_ptr<Expression> right;
    BinaryOp(std::unique_ptr<Expression> l, ASTTokenKind o, std::unique_ptr<Expression> r);
    void accept(Visitor& v) override;
};

}
