#pragma once
#include "../nodes/ASTNode.hpp"
#include <memory>

namespace fin {

class UnaryOp : public Expression {
public:
    ASTTokenKind op;
    std::unique_ptr<Expression> operand;
    UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

}
