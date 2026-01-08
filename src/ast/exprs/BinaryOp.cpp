#include "BinaryOp.hpp"
#include "../Visitor.hpp"

namespace fin {
BinaryOp::BinaryOp(std::unique_ptr<Expression> l, ASTTokenKind o, std::unique_ptr<Expression> r)
    : left(std::move(l)), op(o), right(std::move(r)) {}
void BinaryOp::accept(Visitor& v) { v.visit(*this); }
}
