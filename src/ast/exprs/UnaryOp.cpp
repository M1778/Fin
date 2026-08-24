#include "UnaryOp.hpp"
#include "../Visitor.hpp"

namespace fin {
UnaryOp::UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e, bool postfix)
    : op(o), operand(std::move(e)), is_postfix(postfix) {}
void UnaryOp::accept(Visitor& v) { v.visit(*this); }
}
